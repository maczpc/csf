SHELL	= /bin/sh
COMP	= $(component)
BINDIR	= bin
LIBDIR	= lib
COMPDIR	= component
SYSTYPE	= `$(SHELL) systype.sh`

DIRS	= ./utils
DIRS	+=./csf/confparser
#DIRS	+=./csf/monitor
DIRS	+=./csf
DIRS	+=`if [ ! $(COMP) -o $(COMP) = $(COMPDIR) ]; then echo ''; else echo './$(COMPDIR)'; fi;`


.PHONY: default update clean install clean_all checkdir

default: update

update:	checkdir
	$(MAKE) -f $(SYSTYPE).mk dirs="$(DIRS)" comp="$(COMP)"
	
clean:
	if [ $(COMP) ]; then	\
		$(MAKE) clean -f $(SYSTYPE).mk dirs="$(DIRS)" comp="$(COMP)";	\
	else	\
		$(MAKE) clean_all;	\
		$(MAKE) clean -f $(SYSTYPE).mk dirs="$(DIRS)"; \
	fi;
	
install:
	$(MAKE) install -f $(SYSTYPE).mk dirs="$(DIRS)" comp="$(COMP)"


clean_all:
	rm -f $(LIBDIR)/*
	rm -f $(BINDIR)/*

checkdir:
	@(echo "[checking necessary directory.]");
	if [ ! -d "$(BINDIR)" ]; then	\
		mkdir -p $(BINDIR);	\
	fi;	\
	if [ ! -d "$(LIBDIR)" ]; then	\
		mkdir -p $(LIBDIR);	\
	fi;
	
	

