CC = gcc
AR = ar 
INSTALL	= install

ARFLAGS = rcv
IFLAGS  = -I./confparser -I/usr/include -I./include -I/usr/local/include -I../utils/include
CFLAGS	= $(cflags) -fPIC
PREDEF = $(pdef) -DLINUX_API
LDFLAGS	= -L/usr/local/lib -L../lib -lutils -lconfparser -levent -lpthread -ldl

LINK	= -Wl
MAIN_OBJS	= main.o log.o mempool.o utils.o
PROTOLIB_OBJS	= libprotocol.o mempool.o log.o submit_request.o common.o utils.o monitor.o
MODLIB_OBJS	=  libmod.o log.o pipeline_stage.o common.o utils.o monitor.o
WORKER_OBJS	= server.o tcp.o udp.o protocol.o data.o mempool.o pipeline_stage.o \
	pipeline.o common.o utils.o worker.o module.o log.o  monitor.o

WARN    = $(warn)

.PHONY: clean install

default: csfd-worker csfd libmod.a libprotocol.a 

.c.o:
	$(CC) $(WARN) -c $*.c $(CFLAGS) $(IFLAGS) $(PREDEF)

csfd-worker: 	$(WORKER_OBJS)
	$(CC) $(IFLAGS) -o $@ $(DEBUG) $(WORKER_OBJS) $(LINK) $(LDFLAGS)
	mv -f $@ ../bin/
	
csfd: 	$(MAIN_OBJS)
	$(CC) $(IFLAGS) -o $@ $(DEBUG) $(MAIN_OBJS) $(LINK) $(LDFLAGS)
	mv -f $@ ../bin/

libprotocol.a:	$(PROTOLIB_OBJS)
	$(AR) $(ARFLAGS) $@ $(PROTOLIB_OBJS)
	mv -f $@ ../lib/
	
libmod.a:	$(MODLIB_OBJS)
	$(AR) $(ARFLAGS) $@ $(MODLIB_OBJS)
	mv -f $@ ../lib/


install:
	if [ -x /usr/local/bin ]; then \
		$(INSTALL) -m 755 csfd /usr/local/bin/csfd; \
	else \
		$(INSTALL) -m 755 csfd /usr/bin/csfd; fi

clean:
	rm -f *.o *.swp ../bin/csfd ../bin/csfd-worker ../lib/*.a

