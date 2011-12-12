/*-
 * Copyright (c) 2007-2009 SINA Corporation, All Rights Reserved.
 *  Authors:
 *      Zhu Yan <zhuyan@staff.sina.com.cn>
 *
 */
 
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

#include "log.h"
#include "mempool.h"
#include "utils.h"
#include "comm_proto.h"


void 
_crit(const char *func_name, int line, const char *message) 
{
    WLOG_ERR("%s[%d]: %s", func_name, line, message);
    exit(EXIT_FAILURE);
}

/* We should malloc from a buffer pool */
void*
_smalloc(const char *func_name, int line, int size)
{
	void *rv;

	CSF_UNUSED_ARG(func_name);
	CSF_UNUSED_ARG(line);

	rv = mp_malloc(size);
	//rv = calloc(1, size);
	if(rv == NULL)
		_crit(__func__, __LINE__, "Memory allocation error.");

    return rv;
}

void
_sfree(const char *func_name, int line, void *ptr)
{
	CSF_UNUSED_ARG(func_name);
	CSF_UNUSED_ARG(line);

/*	printf("sfree: %s[%d] %d:%p\n", func_name, line, *c, ptr); */

	//free(ptr);

	mp_free(ptr);
}

int
daemonize(int nochdir, int noclose)
{
	struct sigaction osa, sa;
	int fd;
	pid_t newgrp;
	int oerrno;
	int osa_ok;

	/* A SIGHUP may be thrown when the parent exits below. */
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	osa_ok = sigaction(SIGHUP, &sa, &osa);

	switch (fork()) {
	case -1:
		return (-1);
	case 0:
		break;
	default:
		exit(0);
	}

	newgrp = setsid();
	oerrno = errno;
	if (osa_ok != -1)
		sigaction(SIGHUP, &osa, NULL);

	if (newgrp == -1) {
		errno = oerrno;
		return (CSF_ERR);
	}

	if (!nochdir) {
		(void)chdir("/");
	}

	if (!noclose && (fd = open("/dev/null", O_RDWR, 0)) != -1) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > 2) {
			(void)close(fd);
		}
	}
	return (CSF_OK);
}


