// copyright 2008-2009 paul@ant2.sbrk.co.uk. released under GPLv3
// vers 0.5
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "antdefs.h"

static uchar cbuf[MESG_DATA_SIZE]; // channel event data gets stored here
static uchar ebuf[MESG_DATA_SIZE]; // response event data gets stored here

int start = 0;
int pcpod;

uchar
chevent(uchar chan, uchar event)
{
	static uchar oldseq[2];
	double hrd;
	int rr;
	static int oldrrs[2];
	int oldrr;
	static char olddata[2][MESG_DATA_SIZE];
	int i;
	static int once[2] = {0,0};
	int hroff;
	int seqoff;
	int oldrroff;
	int newrroff;

	switch (event) {
	case EVENT_RX_BROADCAST:
		if (!once[chan]) {
			once[chan] = 1;
			ANT_RequestMessage(chan, MESG_CHANNEL_ID_ID); /* request sender id */
		}
		switch(chan) {
			case 0:
				hroff = 0 + (pcpod?4:0);
				seqoff = 1 + (pcpod?4:0);
				// old2rroff = 6 + pcpod?4:0);
				oldrroff = 4 + (pcpod?4:0);
				newrroff = 2 + (pcpod?4:0);
				break;
			case 1:
				hroff = 7;
				seqoff = 6;
				oldrroff = -1;
				newrroff = 4;
				break;
		}
		//for (i=0; i < MESG_DATA_SIZE; i++)
		//	fprintf(stderr, "%02x", cbuf[i]);
		//fprintf(stderr, "\n");
		if (cbuf[hroff] != 1 && (cbuf[hroff] < 30 || cbuf[hroff] > 240)) {
			// probably not valid
			for (i=0; i < MESG_DATA_SIZE; i++)
				fprintf(stderr, "%02x", cbuf[i]);
			fprintf(stderr, "\n");
		} else if (memcmp(cbuf, olddata[chan], MESG_DATA_SIZE)) {
			memcpy(olddata[chan], cbuf, MESG_DATA_SIZE);
			rr = cbuf[newrroff]+cbuf[newrroff+1]*256;
			if (oldrroff != -1) {
				oldrr = cbuf[oldrroff]+cbuf[oldrroff+1]*256;
				if (oldrrs[chan] != oldrr)
					printf("bad oldrr old %04x new %04x\n", oldrrs[chan], oldrr);
			}
			if (cbuf[seqoff] != (oldseq[chan]+1)%256) {
				printf("Bad seq# old %02x new %02x\n", oldseq[chan], cbuf[seqoff]);
			}
			if (rr-oldrrs[chan] < 0)
				hrd = 60.0*1024.0/(double)(rr+65536-oldrrs[chan]);
			else if (rr > oldrrs[chan])
				hrd = 60.0*1024.0/(double)(rr-oldrrs[chan]);
			else
				hrd = 0.0;
			printf("%d:%03d HR %d hr-rr %g\n", chan, cbuf[seqoff], cbuf[hroff], hrd);
			fflush(stdout);
			oldrrs[chan] = rr;
			oldseq[chan] = cbuf[seqoff];
		}
		break;
	}
	return 1;
}

uchar
revent(uchar chan, uchar event)
{
	ushort devid;

	if (event == 0 && ebuf[1] == MESG_ASSIGN_CHANNEL_ID) {
		printf("Installing cheventfn\n");
		ANT_AssignChannelEventFunction(chan, chevent, cbuf);
		return 0;
	} else if (event == MESG_CHANNEL_ID_ID) {
		devid = ebuf[1]+ebuf[2]*256;
		printf("devid %04x\n", devid);
	} else if (event == 0x3d) {
		ebuf[9] = 0;
		printf("received %s\n", ebuf);
		pcpod = 1;
		start = 1;
	} else if (event == INVALID_MESSAGE && ebuf[1] == MESG_REQUEST_ID) {
		printf("chan %d event %x %x %x\n", chan, event, ebuf[0], ebuf[1]);
		// not a PC POD
		pcpod = 0;
		start = 1;
		printf("not a pcpod %d\n", pcpod);
	} else {
		//printf("xchan %d event %x %x %x\n", chan, event, ebuf[0], ebuf[1]);
	}
	return 1;
}

char *progname;

static uchar GARMIN_KEY[] = "B9A521FBBD72C345"; // maybe other ANT+ HRMs too
static uchar SUUNTO_KEY[] = "B9AD3228757EC74D"; // Suunto HRM

int
main(int ac, char *av[])
{
	int chan0 = 0;
	int net0 = 1;

	int chan1 = 1;
	int net1 = 0;

	int chtype = 0; // wildcard
	int devno = 0; // wildcard
	int devtype = 0; // wildcard
	int manid = 0; // wildcard
	int gfreq = 0x39; //  garmin radio frequency
	int gperiod = 0x1f86; // garmin search period
	int sfreq = 0x41; //  suunto radio frequency
	int speriod = 0x199a; // suunto search period
	int srchto = 50; // max timeout
	int suunto = 0;
	int garmin = 0;

	int c;
	extern char *optarg;
	extern int optind, opterr, optopt;
	char *devfile = "/dev/ttyUSB0"; // default

	progname = av[0];
	while ((c = getopt(ac, av, "sgf:t:")) != -1) {
		switch(c) {
			case 'f':
				devfile = optarg;
				break;
			case 's':
				suunto = 1;
				break;
			case 'g':
				garmin = 1;
				break;
			case 't':
				srchto = atoi(optarg);
				break;
		}
	}

	if (!ANT_Initf(devfile, 0))
		exit(1);
	ANT_ResetSystem();
	ANT_AssignResponseFunction(revent, ebuf);
	ANT_RequestMessage(chan0, MESG_CAPABILITIES_ID);
	ANT_RequestMessage(chan0, 0x3d); // See if this is a Suunto PC POD
	// wait for an answer
	while (start != 1)
		usleep(100*1000);
	if (suunto && pcpod) {
		ANT_AssignChannel(chan0, chtype, net0);
		ANT_SetChannelId(chan0, devno, devtype, manid);
		ANT_SetNetworkKeya(net0, SUUNTO_KEY);
		// think these are actually set in the 0x3d message above
		//ANT_SetChannelSearchTimeout(chan, srchto);
		//ANT_SetChannelPeriod(chan, period);
		//ANT_SetChannelRFFreq(chan, freq);

		ANT_Cmd55(chan0); // suunto specific search function
	} else {
		ANT_AssignChannel(chan0, chtype, net0);
		ANT_SetChannelId(chan0, devno, devtype, manid);
		ANT_SetNetworkKeya(net0, SUUNTO_KEY);
		ANT_SetChannelSearchTimeout(chan0, srchto);
		ANT_SetChannelPeriod(chan0, speriod);
		ANT_SetChannelRFFreq(chan0, sfreq);

		ANT_AssignChannel(chan1, chtype, net1);
		ANT_SetChannelId(chan1, devno, devtype, manid);
		ANT_SetNetworkKeya(net1, GARMIN_KEY);
		ANT_SetChannelSearchTimeout(chan1, srchto);
		ANT_SetChannelPeriod(chan1, gperiod);
		ANT_SetChannelRFFreq(chan1, gfreq);

		if (suunto) ANT_OpenChannel(chan0);
		if (garmin) ANT_OpenChannel(chan1);
	}
	for(;;)
	sleep(10);
	return 0;
}

// vim: se ts=2 sw=2:
