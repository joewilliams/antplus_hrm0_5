// copyright 2008-2009 paul@ant2.sbrk.co.uk. released under GPLv3
// vers 0.5
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <termios.h>
#include <stdlib.h>

#include "antdefs.h"

#define S(e) if (-1 == (e)) {perror(#e);exit(1);} else

#define MAXMSG 12 // SYNC,LEN,MSG,data[8],CHKSUM
#define uchar unsigned char

#define hexval(c) ((c >= '0' && c <= '9') ? (c-'0') : ((c&0xdf)-'A'+10))

static int fd = -1;
static int dbg = 0;
static pthread_t commthread;;

static RESPONSE_FUNC rfn = 0;
static uchar *rbufp;
static CHANNEL_EVENT_FUNC cfn = 0;
static uchar *cbufp;

struct msg_queue {
	uchar msgid;
	uchar len;
};

// send message over serial port
uchar
msg_send(uchar mesg, uchar *inbuf, uchar len)
{
	uchar buf[MAXMSG];
	ssize_t nw;
	int i;
	uchar chk = MESG_TX_SYNC;

	buf[0] = MESG_TX_SYNC;
	buf[1] = len; chk ^= len;
	buf[2] = mesg; chk ^= mesg;
	for (i = 0; i < len; i++) {
		buf[3+i] = inbuf[i];
		chk ^= inbuf[i];
	}
	buf[3+i] = chk;
	usleep(10*1000);
	if (4+i != (nw=write(fd, buf, 4+i))) {
		if (dbg) {
			perror("failed write");
		}
	}
	return 1;
}

// two argument send
uchar
msg_send2(uchar mesg, uchar data1, uchar data2)
{
	uchar buf[2];
	buf[0] = data1;
	buf[1] = data2;
	return msg_send(mesg, buf, 2);
}

// three argument send
uchar
msg_send3(uchar mesg, uchar data1, uchar data2, uchar data3)
{
	uchar buf[3];
	buf[0] = data1;
	buf[1] = data2;
	buf[2] = data3;
	return msg_send(mesg, buf, 3);
}

void
get_data(int fd)
{
	static uchar buf[500];
	static int bufc = 0;
	int nr;
	int dlen;
	int i;
	int j;
	unsigned char chk = 0;
	uchar event;
	int found;
	int srch;
	int next;

	nr = read(fd, buf+bufc, 20);
	if (nr > 0)
		bufc += nr;
	else
		return;
	if (bufc > 30) {
		if (dbg)
			fprintf(stderr, "bufc %d\n", bufc);
	}
	if (bufc > 300) {
		fprintf(stderr, "buf too long %d\n", bufc);
		for (j = 0; j < bufc; j++)
			fprintf(stderr, "%02x", buf[j]);
		fprintf(stderr, "\n");
		exit(1);
	}

	// some data in buf
	// search for possible valid messages
  srch = 0;
	while (srch < bufc) {
		found = 0;
		//printf("srch %d bufc %d\n", srch, bufc);
		for (i = srch; i < bufc; i++) {
			if (buf[i] == MESG_TX_SYNC) {
				//fprintf(stderr, "bufc %d sync %d\n", bufc, i);
				if (i+1 < bufc && buf[i+1] >= 1 && buf[i+1] <= 13) {
					dlen = buf[i+1];
					if (i+3+dlen < bufc) {
						chk = 0;
						for (j = i; j <= i+3+dlen; j++)
							chk ^= buf[j];
						if (0 == chk) {
							found = 1; // got a valid message
							break;
						} else {
							fprintf(stderr, "bad chk %02x\n", chk);
							for (j = i; j <= i+3+dlen; j++)
								fprintf(stderr, "%02x", buf[j]);
							fprintf(stderr, "\n");
						}
					}
				}
			}
		}
		if (found) {
			next = j;
			//printf("next %d %02x\n", next, buf[j-1]);
			// got a valid message, see if any data needs to be discarded
			if (i > srch) {
				fprintf(stderr, "\nDiscarding: ");
				for (j = 0; j < i; j++)
					fprintf(stderr, "%02x", buf[j]);
				fprintf(stderr, "\n");
			}

			if (dbg) {
				fprintf(stderr, "data: ");
				for(j = i; j < i+dlen+4; j++) {
					fprintf(stderr, "%02x", buf[j]);
				}
				fprintf(stderr, "\n");
			}
			event = 0;
			switch (buf[i+2]) {
			case MESG_RESPONSE_EVENT_ID:
					//if (cfn) {
					//	memcpy(cbufp, buf+i+4, dlen);
					//	(*cfn)(buf[i+3], buf[i+5]);
					//	else
					if (rfn) {
						memcpy(rbufp, buf+i+3, dlen);
						(*rfn)(buf[i+3], buf[i+5]);
					} else {
						if (dbg)
							fprintf(stderr, "no rfn or cfn\n");
					}
					break;
				case MESG_BROADCAST_DATA_ID:
					event = EVENT_RX_BROADCAST;
					break;
				case MESG_ACKNOWLEDGED_DATA_ID:
					event = EVENT_RX_ACKNOWLEDGED;
					break;
				case MESG_BURST_DATA_ID:
					event = EVENT_RX_BURST_PACKET;
					break;
				case MESG_EXT_BROADCAST_DATA_ID:
					event = EVENT_RX_EXT_BROADCAST;
					break;
				case MESG_EXT_ACKNOWLEDGED_DATA_ID:
					event = EVENT_RX_EXT_ACKNOWLEDGED;
					break;
				case MESG_EXT_BURST_DATA_ID:
					event = EVENT_RX_EXT_BURST_PACKET;
					break;
				default:
					if (rfn) {
						// should be this according to the docs, but doesn't fit
						// if (dlen > MESG_RESPONSE_EVENT_SIZE) {
						 if (dlen > MESG_DATA_SIZE) {
							fprintf(stderr, "cresponse buffer too small %d > %d\n", dlen, MESG_DATA_SIZE);
							for (j = 0; j < dlen; j++)
								fprintf(stderr, "%02x", *(buf+i+3+j));
							fprintf(stderr, "\n");
							exit(1);
						}
						memcpy(rbufp, buf+i+3, dlen);
						(*rfn)(buf[i+3], buf[i+2]);
					} else
						if (dbg)
							fprintf(stderr, "no rfn\n");
			}
			if (event) {
				if (cfn) {
					if (dlen > MESG_DATA_SIZE) {
						fprintf(stderr, "rresponse buffer too small %d > %d\n",
							dlen, MESG_DATA_SIZE);
						for (j = 0; j < dlen+1; j++)
							fprintf(stderr, "%02x", *(buf+i+4+j));
						fprintf(stderr, "\n");
						exit(1);
					}
					memcpy(cbufp, buf+i+4, dlen);
					(*cfn)(buf[i+3], event);
				} else
					if (dbg)
						fprintf(stderr, "no cfn\n");
			}
			srch = next;
		} else 
			break;
	}
	if (next < bufc) {
		memmove(buf, buf+next, bufc-next);
		bufc -= next;
	} else
		bufc = 0;
}

void *commfn(void* arg)
{
	fd_set readfds, writefds, exceptfds;
	int ready;
	struct timeval to;

	for(;;) {
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);
		FD_SET(fd, &readfds);
		to.tv_sec = 1;
		to.tv_usec = 0;
		ready = select(fd+1, &readfds, &writefds, &exceptfds, &to);
		if (ready) {
			get_data(fd);
		}
	}
}

uchar
ANT_ResetSystem(void)
{
	uchar filler = 0;
	return msg_send(MESG_SYSTEM_RESET_ID, &filler, 1);
}

uchar
ANT_Cmd55(uchar chan)
{
	return msg_send(0x55, &chan, 1);
}

uchar
ANT_OpenRxScanMode(uchar chan)
{
	return msg_send(MESG_OPEN_RX_SCAN_ID, &chan, 1);
}

uchar
ANT_Initf(char *devname, ushort baud)
{
	struct termios tp;

	fd = open(devname, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		perror(devname);
		return 0;
	}

	S(tcgetattr(fd, &tp));
	tp.c_iflag &=
	~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IXOFF|IXANY|INPCK|IUCLC);
	tp.c_oflag &= ~OPOST;
	tp.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN|ECHOE);
	tp.c_cflag &= ~(CSIZE|PARENB);
	tp.c_cflag |= CS8 | CLOCAL | CREAD; // | CRTSCTS;

	S(cfsetispeed(&tp, B115200));
	S(cfsetospeed(&tp, B115200));
	tp.c_cc[VMIN] = 1;
	tp.c_cc[VTIME] = 0;
	S(tcsetattr(fd, TCSANOW, &tp));

	if (pthread_create(&commthread, 0, commfn, 0));
	return 1;
}

uchar
ANT_Init(uchar devno, ushort baud)
{
	char dev[40];

	sprintf(dev, "/dev/ttyUSB%d", devno);
	return ANT_Initf(dev, devno);
}

uchar
ANT_RequestMessage(uchar chan, uchar mesg)
{
	return msg_send2(MESG_REQUEST_ID, chan, mesg);
}

uchar
ANT_SetNetworkKeya(uchar net, uchar *key)
{
	uchar buf[9];
	int i;

	if (strlen((char *)key) != 16) {
		fprintf(stderr, "Bad key length %s\n", key);
		return 0;
	}
	buf[0] = net;
	for (i = 0; i < 8; i++)
		buf[1+i] = hexval(key[i*2])*16+hexval(key[i*2+1]);
	return msg_send(MESG_NETWORK_KEY_ID, buf, 9);
}

uchar
ANT_SetNetworkKey(uchar net, uchar *key)
{
	uchar buf[9];

	buf[0] = net;
	memcpy(buf+1, key, 8);
	return msg_send(MESG_NETWORK_KEY_ID, buf, 9);
}

uchar
ANT_AssignChannel(uchar chan, uchar chtype, uchar net)
{
	return msg_send3(MESG_ASSIGN_CHANNEL_ID, chan, chtype, net);
}

uchar
ANT_UnAssignChannel(uchar chan)
{
	return msg_send(MESG_UNASSIGN_CHANNEL_ID, &chan, 1);
}

uchar
ANT_SetChannelId(uchar chan, ushort dev, uchar devtype, uchar manid)
{
	uchar buf[5];
	buf[0] = chan;
	buf[1] = dev%256;
	buf[2] = dev/256;
	buf[3] = devtype;
	buf[4] = manid;
	return msg_send(MESG_CHANNEL_ID_ID, buf, 5);
}

uchar
ANT_SetChannelRFFreq(uchar chan, uchar freq)
{
	return msg_send2(MESG_CHANNEL_RADIO_FREQ_ID, chan, freq);
}

uchar
ANT_SetChannelPeriod(uchar chan, ushort period)
{
	uchar buf[3];
	buf[0] = chan;
	buf[1] = period%256;
	buf[2] = period/256;
	return msg_send(MESG_CHANNEL_MESG_PERIOD_ID, buf, 3);
}

uchar
ANT_SetChannelSearchTimeout(uchar chan, uchar timeout)
{
	return msg_send2(MESG_CHANNEL_SEARCH_TIMEOUT_ID, chan, timeout);
}

uchar
ANT_SetSearchWaveform(uchar chan, ushort waveform)
{
	uchar buf[3];
	buf[0] = chan;
	buf[1] = waveform%256;
	buf[2] = waveform/256;
	return msg_send(MESG_SEARCH_WAVEFORM_ID, buf, 3);
}

uchar
ANT_SendAcknowledgedDataA(uchar chan, uchar *data) // ascii version
{
	uchar buf[9];
	int i;

	if (strlen((char *)data) != 16) {
		fprintf(stderr, "Bad data length %s\n", data);
		return 0;
	}
	buf[0] = chan;
	for (i = 0; i < 8; i++)
		buf[1+i] = hexval(data[i*2])*16+hexval(data[i*2+1]);
	return msg_send(MESG_ACKNOWLEDGED_DATA_ID, buf, 9);
}

uchar
ANT_SendAcknowledgedData(uchar chan, uchar *data)
{
	uchar buf[9];

	buf[0] = chan;
	memcpy(buf+1, data, 8);
	return msg_send(MESG_ACKNOWLEDGED_DATA_ID, buf, 9);
}

ushort
ANT_SendBurstTransferA(uchar chan, uchar *data, ushort numpkts)
{
	uchar buf[9];
	int i;
	int j;
	int seq = 0;

	fprintf(stderr, "numpkts %d data %s\n", numpkts, data);
	if (strlen((char *)data) != 16*numpkts) {
		fprintf(stderr, "Bad data length %s numpkts %d\n", data, numpkts);
		return 0;
	}
	for (j = 0; j < numpkts; j++) {
		buf[0] = chan|(seq<<5)|(j==numpkts-1 ? 0x80 : 0);
		for (i = 0; i < 8; i++)
			buf[1+i] = hexval(data[j*16+i*2])*16+hexval(data[j*16+i*2+1]);
		usleep(20*1000);
		msg_send(MESG_BURST_DATA_ID, buf, 9);
		seq++; if (seq > 3) seq = 1;
	}
	return numpkts;
}

ushort
ANT_SendBurstTransfer(uchar chan, uchar *data, ushort numpkts)
{
	uchar buf[9];
	int j;
	int seq = 0;

	for (j = 0; j < numpkts; j++) {
		buf[0] = chan|(seq<<5)|(j==numpkts-1 ? 0x80 : 0);
		memcpy(buf+1, data+j*8, 8);
		usleep(20*1000);
		msg_send(MESG_BURST_DATA_ID, buf, 9);
		seq++; if (seq > 3) seq = 1;
	}
	return numpkts;
}

uchar
ANT_OpenChannel(uchar chan)
{
	return msg_send(MESG_OPEN_CHANNEL_ID, &chan, 1);
}

uchar
ANT_CloseChannel(uchar chan)
{
	return msg_send(MESG_CLOSE_CHANNEL_ID, &chan, 1);
}

void
ANT_AssignResponseFunction(RESPONSE_FUNC rf, uchar* rbuf)
{
	rfn = rf;
	rbufp = rbuf;
}

void
ANT_AssignChannelEventFunction(uchar chan, CHANNEL_EVENT_FUNC rf, uchar* rbuf)
{
	cfn = rf;
	cbufp = rbuf;
}

int ANT_fd()
{
	return fd;
}

// vim: se ts=2 sw=2:
