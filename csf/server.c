/*-
 * Copyright (c) 2007-2008 SINA Corporation, All Rights Reserved.
 *  Authors:
 *      Zhu Yan <zhuyan@staff.sina.com.cn>
 *
 * Server Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <event.h>
#include <netinet/in.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <dlfcn.h>
#include <pwd.h>
#include <grp.h>

#include "pipeline.h"
#include "data.h"
#include "queue.h"
#include "server.h"
#include "log.h"
#include "confparser.h"
#include "iniparser.h"
//#include "common.h"
#include "utils.h"
#include "protocol.h"
#include "monitor.h"

int done_fd = 0;
int done_fd_read = 0;

int local_fd = 0;
int local_fd_read = 0;


void 
conn_state_cleanup(CONN_STATE *csp)
{
	(void)connection_lost(csp);
	/* delete the event */
	(void)event_del(&(csp->ev));

	CS_RELE(csp);
}


static int
set_user_group(char *user, char *group)
{
	struct group *grp = NULL;
	struct passwd *pwd = NULL;
	int i_am_root = (getuid() == 0);

	if (user == NULL) {
		return (CSF_ERR);
	}

	if (group == NULL) {
		return (CSF_ERR);
	}

	if (i_am_root) {
		/* set user and group */
		if (user[0] != '\0') {
			if (NULL == (pwd = getpwnam(user))) {
				WLOG_ERR("can't find username: %s", user);
				return (CSF_ERR);
			}
			if (pwd->pw_uid == 0) {
				WLOG_ERR("I will not set uid to 0");
				return (CSF_ERR);
			}
		}
		if (group[0] != '\0') {
			if (NULL == (grp = getgrnam(group))) {
				WLOG_ERR("can't find groupname: %s", group);
				return (CSF_ERR);
			}
			if (grp->gr_gid == 0) {
				WLOG_ERR("I will not set gid to 0");
				return (CSF_ERR);
			}
		}

		/* drop root privs */
		if (group[0] != '\0') {
			setgid(grp->gr_gid);
		}
		if (user[0] != '\0') {
			if (group[0] != '\0') {
				initgroups(user, grp->gr_gid);
			}
			setuid(pwd->pw_uid);
		}
	}

	return (CSF_OK);
}

static void 
local_requests_handler(int fd, short event, void *arg)
{
	CSF_UNUSED_ARG(fd);
	CSF_UNUSED_ARG(event);
	CSF_UNUSED_ARG(arg);
}

static int
event_handler_init(int sockfd, SOCKET_EVENT_HANDLER *socket_event_handler)
{
	struct event	*ev_sock;
	struct event	*ev_pipe;
	struct event	*ev_local;
	int				done_pipe[2];
	int				local_pipe[2];
	int				rc;
	struct timeval		tv;
	
	/* Set event monitor */
	rc = pipe(done_pipe);
	if (rc < 0) {
		WLOG_ERR("Can't create pipe");
		return rc;
	}
	done_fd = done_pipe[1];
	done_fd_read = done_pipe[0];

	/* Set nonblocked */
	rc = fcntl(done_fd, F_GETFL, 0);
	if (rc < 0) {
		WLOG_ERR("fcntl() error: %s", strerror(errno));
		return rc;
	}
	(void)fcntl(done_fd, F_SETFL, rc | O_NONBLOCK);

	rc = fcntl(done_fd_read, F_GETFL, 0);
	if (rc < 0) {
		return rc;
	}
	(void)fcntl(done_fd_read, F_SETFL, rc | O_NONBLOCK);

	/* Set event monitor */
	rc = pipe(local_pipe);
	if (rc < 0) {
		WLOG_ERR("Can't create pipe");
		return rc;
	}
	local_fd = local_pipe[1];
	local_fd_read = local_pipe[0];

	/* Set nonblocked */
	rc = fcntl(local_fd, F_GETFL, 0);
	if (rc < 0) {
		WLOG_ERR("fcntl() error: %s", strerror(errno));
		return rc;
	}
	(void)fcntl(local_fd, F_SETFL, rc | O_NONBLOCK);

	rc = fcntl(local_fd_read, F_GETFL, 0);
	if (rc < 0) {
		return rc;
	}

	(void)fcntl(local_fd_read, F_SETFL, rc | O_NONBLOCK);

	ev_pipe = (struct event *)calloc(1, sizeof(struct event));
	if (ev_pipe == NULL) {
		CRIT("Can't calloc ev_pipe!");
	}

	ev_local = (struct event *)calloc(1, sizeof(struct event));
	if (ev_local == NULL) {
		CRIT("Can't calloc ev_local!");
	}

	ev_sock = (struct event *)calloc(1, sizeof(struct event));
	if (ev_sock == NULL) {
		CRIT("Can't calloc ev_sock!");
	}


	/* Initalize the event library */
    (void)event_init();

	event_set(ev_pipe,
		done_fd_read,
		EV_READ|EV_PERSIST,
		done_requests_handler,
		ev_pipe);

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	(void)event_add(ev_pipe, &tv);

	event_set(ev_local,
		local_fd_read,
		EV_READ,
		local_requests_handler,
		ev_local);
	(void)event_add(ev_local, NULL);

	/* Initalize one event */
	(void)event_set(ev_sock,
		sockfd,
		EV_READ|EV_PERSIST,
		socket_event_handler,
		ev_sock);

	/* Add it to the active events, without a timeout  */
	(void)event_add(ev_sock, NULL);

	while (1) {
		rc = event_dispatch();
		WLOG_ERR("event_dispatch() exit! errno: %d, retval: %d", errno, rc);
	}
	return (CSF_OK);
}

extern int connection_counter;

void
server_init(CSF_CONF *conf)
{
	struct sigaction sa;
	int fd;
	int ret;
	COMM_PROTO_OPS *ops;

	ops = conf->cp_ops;
	
	sa.sa_handler = SIG_IGN;
	if (sigemptyset(&sa.sa_mask) < 0 ||
		sigaction(SIGPIPE, &sa, 0) < 0) {
		CRIT("failed to ignore SIGPIPE sigaction");
	}

	//monitor_var_register("main", "connections", &connection_counter, MNT_TYPE_INT);

	if (ops->proto_init != NULL) {
		fd = ops->proto_init(conf->server_port, 
			conf->bind_ip, conf->server_timeout);

		if (fd < 0) {
			WLOG_ERR("socket error!");
			return;
		}
	} else {
		WLOG_ERR("proto_init is NULL!");
		return;
	}

	ret = set_user_group(conf->user, conf->group);

	if (ret != CSF_OK) {
		WLOG_ERR("Can't set user or group!");
		return;
	}

	event_handler_init(fd, conf->cp_ops->event_handler);
}

