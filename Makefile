CFLAGS=-g -Werror -fstack-protector-all -DFORTIFY_SOURCE=2 -Wall
LDFLAGS=-lpthread

all:	hrm

hrm: hrm.o antlib.o antdefs.h

clean:
	rm *.o hrm
