SHELL	= /bin/sh
COMP	= $(comp)
CFLAGS	= $(cflags) -fPIC
PREDEF	= $(pdef)
WARN	= $(warn)
VARLIST	= cflags="$(CFLAGS)" pdef="$(PREDEF)" comp="$(COMP)" warn="$(WARN)"

defalut:update

update: check_dir
	@(echo "start to compile component of CSF"); 
	@(set -e; cd $(COMP); \
	 $(MAKE) -f freebsd.mk $(VARLIST);) || exit 1;

clean: check_dir
	@(echo "clean");
	(set -e; cd $(COMP); \
	 $(MAKE) clean -f freebsd.mk) || exit 1;

install: check_dir
	@(echo "install");
	(set -e; cd $(COMP); \
	 $(MAKE) install -f freebsd.mk) || exit 1;

check_dir:
	@(set -e; \
	 if [ ! -d "$(COMP)" ]; then	\
		printf "\n*** Error\n\nDirectory $(COMP) NOT FOUND.\n\n";	\
		exit 1;	fi;)
