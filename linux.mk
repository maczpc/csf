SHELL	= /bin/sh
DIRS	= $(dirs)
COMP	= $(comp)
CFLAGS = -g -O2
PREDEF = -DWLOG_ -DDEBUG_ -D_THREAD_SAFE

WARN_GCC4 = -Wsystem-headers -Wall -Wno-format-y2k -W \
	-Wno-unused-parameter -Wstrict-prototypes \
	-Wmissing-prototypes -Wpointer-arith -Wreturn-type \
	-Wcast-qual -Wwrite-strings -Wswitch -Wshadow \
	-Wcast-align -Wunused-parameter -Wchar-subscripts \
	-Winline -Wnested-externs -Wredundant-decls
	
WARN_GCC3 = 
	
WARN = $(WARN_GCC4)


VARLIST	= cflags="$(CFLAGS)" pdef="$(PREDEF)" comp="$(COMP)" warn="$(WARN)"

.PHONY: default update clean install

default: update

update: 
	for i in $(DIRS); do \
		(cd $$i && $(MAKE) -f linux.mk $(VARLIST)) || exit 1; \
	done
	
clean:
	for i in $(DIRS); do \
		(cd $$i && $(MAKE) clean -f linux.mk $(VARLIST)) || exit 1; \
	done;
	
install:
	for i in $(DIRS); do \
		(cd $$i && $(MAKE) install -f linux.mk $(VARLIST)) || exit 1; \
	done;
