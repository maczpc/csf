CC = gcc
AR = ar 
INSTALL	= install

ARFLAGS = rcv
IFLAGS  = 
CFLAGS	= $(cflags) -fPIC
PREDEF = $(pdef) -DLINUX_API
LDFLAGS	= 

LINK	= -Wl
UTILS_OBJS	= strlcpy.o strlcat.o

WARN    = $(warn)

.PHONY: clean install

default: libutils.a 

.c.o:
	$(CC) $(WARN) -c $*.c $(CFLAGS) $(IFLAGS) $(PREDEF)

libutils.a:	$(UTILS_OBJS)
	$(AR) $(ARFLAGS) $@ $(UTILS_OBJS)
	mv -f $@ ../lib/

clean:
	rm -f *.o *.swp 

