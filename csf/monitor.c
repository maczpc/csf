/*-
 * Copyright (c) 2007-2009 SINA Corporation, All Rights Reserved.
 *
 * CSF Monitor: An monitoring module.
 *		Version 2.4 zhangshuo@staff.sina.com.cn
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <arpa/inet.h>

//#include "common.h"
#include "utils.h"
#include "queue.h"
#include "monitor.h"
#include "confparser.h"

#ifndef POLLWRNORM
#define	POLLWRNORM	POLLOUT
#define POLLRDNORM	POLLIN
#endif

/**********************************************************************
 *
 * definations and declarations
 *
 **********************************************************************/
/* #define DEBUG */
#define _VERSION_INFO_  "Version 2.4"

#define MAX_OPEN_FD     10
#define MAX_STR_LEN		512
#define DEFAULT_BINDIP	"0.0.0.0"

/* the node struct of monitor list */
typedef struct entry		
{
	char item_name[MAX_FIELD_LENGTH];
	char so_name[MAX_FIELD_LENGTH];
	void *val;
	int option;
	SLIST_ENTRY(entry) entries;
} VAR_NODE;


/*********************************
 * command processing part
 *********************************/

/* the command and its context, 
 * which is used pass parameters to process function */
typedef struct cmd_context
{
	char *command;					/* command pointer */
	int listenfd;			
	int connfd;
	struct pollfd *all_connfd;		/* all the connected fds */
	size_t all_connfd_len;			/* all the connected fds' length */
}COMMAND_CONTEXT;

/* the pointer type of processing function */
typedef void CMD_PROCESSING(COMMAND_CONTEXT *);

/* the "command <-> function" mapping list */
typedef struct cmd_function
{
	const char *command;			/* command name */
	CMD_PROCESSING *func_ptr;		/* pointer to the proceesing function */
} CMD_FUNCTION;

/*********************************
 * function declaration part
 *********************************/
 
static void show_welcome(int);	
static void monitor_start(int);				/* start the monitor */	
static int accept_new_client(int, struct pollfd *);
static void cmd_stat(COMMAND_CONTEXT *);
static void cmd_help(COMMAND_CONTEXT *);
static void cmd_stop(COMMAND_CONTEXT *);
static void cmd_list(COMMAND_CONTEXT *);
static void cmd_empty(COMMAND_CONTEXT *);
static void command_distribute(COMMAND_CONTEXT *);
static VAR_NODE* add_node(const char *, void *, const char *, int);
void remove_node(void *);
void *monitor_thread(void);	                /* monitor's thread entrance */
int unix_listen(uint16_t, char *);			/* Listen the specific port */
int unix_accept(int);							/* accept the specific connection */
ssize_t	readn(int, void *, size_t);			/* read n bytes */
ssize_t	writen(int, const void *, size_t);
ssize_t readline(int, void *, size_t);			/* read a line */


/*********************************
 * initialization part
 *********************************/

static SLIST_HEAD(slisthead, entry) *monitor_head;
static unsigned char switch_monitor_run = 1;	/* when 0, monitor returns */
static int mnt_fd = -1;
static int monitor_port = -1;
static int monitor_enable = 0;
static char monitor_bind_ip[CONF_ITEM_LEN + 1];

/* "command <-> function" array */
static CMD_FUNCTION cmd_func_array[] = {
	{"stat", cmd_stat},
	{"help", cmd_help},
	{"stop", cmd_stop},
	{"list", cmd_list},
	{"empty", cmd_empty},
	{NULL, NULL}
};

static struct conf_int_config conf_int_array[] = {
	{"monitor_port", &monitor_port},
	{"monitor_enable", &monitor_enable},
	{0, 0}
};

static struct conf_str_config conf_str_array[] = {
	{"monitor_bind_ip", monitor_bind_ip},
	{0, 0}
};

/*********************************
 * command part
 *********************************/

/* distribute the commands to functions */
static void 
command_distribute(COMMAND_CONTEXT *cmd_ctxt)
{
	CMD_FUNCTION *cmd_function;
	size_t str_len;
	char b[] = "Command not found.\r\n\r\n";
	
	cmd_function = cmd_func_array;
	while (cmd_function->command != NULL)
	{
		str_len = strlen(cmd_function->command);
		/* compare the input command with the function array's items */
		if (strncmp(cmd_ctxt->command, cmd_function->command, str_len) == 0) {
			//WLOG_INFO("[monitor] command will be executed: %s; from fd: %d", cmd_ctxt->command, cmd_ctxt->connfd);
			
			/* note that this is a function pointer. type: CMD_FUNCTION*  */
			cmd_function->func_ptr(cmd_ctxt);
			break;
		}
		cmd_function++;
		if (cmd_function->command == NULL) {
			writen(cmd_ctxt->connfd, b, strlen(b));
		}
	}
}

/* stat command */
static void
cmd_stat(COMMAND_CONTEXT *cmd_ctxt)
{
	VAR_NODE *t_node;
	char buf[CMD_BUF_SIZE];
	char arg[CMD_BUF_SIZE];
	char e_opt[CMD_BUF_SIZE];
	
	/* parse stat command.  stat xxxx -e bbbbb */
	memset(arg, '\0', sizeof(arg));
	memset(e_opt, '\0', sizeof(e_opt));
	sscanf(cmd_ctxt->command, "%*[^-]-e%s", e_opt); 
	sscanf(cmd_ctxt->command, "stat%[^-]", arg); 
	
	SLIST_FOREACH(t_node, monitor_head, entries)
	{	
		/* if contains "all", output all */
		if (strstr(arg, "all") != NULL || strlen(arg) == 1) {
			if (strstr(e_opt, t_node->so_name) != NULL) {
				continue;
			}
		}
		/* else output the contents specified by command */
		else if ((strstr(arg, t_node->so_name) == NULL) 
			|| (strstr(e_opt, t_node->so_name) != NULL))
		{
			continue;
		}
		
		/* outputs follow the format below */
		memset(buf, '\0', sizeof(buf));
		
		/* check the pointer type of val */	
		if ((t_node->option & MNT_UINT_64) == MNT_UINT_64) {
			if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%ju,%s##|",     \
					t_node->item_name, (uintmax_t)(*(uint64_t *)(t_node->val)), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%ju,%s|",     \
					t_node->item_name, (uintmax_t)(*(uint64_t *)(t_node->val)), t_node->so_name);
			}
		} else if ((t_node->option & MNT_INT_64) == MNT_INT_64) {
            if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%jd,%s##|",     \
					t_node->item_name, (intmax_t)(*(int64_t *)(t_node->val)), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%jd,%s|",     \
					t_node->item_name, (intmax_t)(*(int64_t *)(t_node->val)), t_node->so_name);
			}
		} else if ((t_node->option & MNT_UINT_32) == MNT_UINT_32) {
            if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%ju,%s##|",     \
					t_node->item_name, (uintmax_t)(*(uint32_t *)(t_node->val)), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%ju,%s|",     \
					t_node->item_name, (uintmax_t)(*(uint32_t *)(t_node->val)), t_node->so_name);
			}
		} else if ((t_node->option & MNT_INT_32) == MNT_INT_32) {
            if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%jd,%s##|",     \
					t_node->item_name, (intmax_t)(*(int32_t *)(t_node->val)), t_node->so_name);
			} else {
			;	snprintf(buf, sizeof(buf), "%s,%jd,%s|",     \
					t_node->item_name, (intmax_t)(*(int32_t *)(t_node->val)), t_node->so_name);
			}
		} else if ((t_node->option & MNT_UINT) == MNT_UINT) {
            if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%ju,%s##|",     \
					t_node->item_name, (uintmax_t)(*(unsigned int *)(t_node->val)), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%ju,%s|",     \
					t_node->item_name, (uintmax_t)(*(unsigned int *)(t_node->val)), t_node->so_name);
			}
		} else if ((t_node->option & MNT_INT) == MNT_INT) {
            if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%jd,%s##|",     \
					t_node->item_name, (intmax_t)(*(signed int *)(t_node->val)), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%jd,%s|",     \
					t_node->item_name, (intmax_t)(*(signed int *)(t_node->val)), t_node->so_name);
			}
		} else if ((t_node->option & MNT_CHAR) == MNT_CHAR) {				
			if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%c,%s##|",     \
					t_node->item_name, *((char *)t_node->val), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%c,%s|",     \
					t_node->item_name, *((char *)t_node->val), t_node->so_name);
			}
		} else if ((t_node->option & MNT_STRING) == MNT_STRING) {
            if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%s,%s##|",     \
					t_node->item_name, (char *)t_node->val, t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%s,%s|",     \
					t_node->item_name, (char *)t_node->val, t_node->so_name);
			}
		} else if ((t_node->option & MNT_FLOAT) == MNT_FLOAT) {				
			if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {
				snprintf(buf, sizeof(buf), "%s,%f,%s##|",     \
					t_node->item_name, *((float *)t_node->val), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%f,%s|",     \
					t_node->item_name, *((float *)t_node->val), t_node->so_name);
			}
		} else if ((t_node->option & MNT_DOUBLE) == MNT_DOUBLE) {				
			if ((t_node->option & MNT_GROUP_STAT) == MNT_GROUP_STAT) {	
				snprintf(buf, sizeof(buf), "%s,%f,%s##|",     \
					t_node->item_name, *((double *)t_node->val), t_node->so_name);
			} else {
				snprintf(buf, sizeof(buf), "%s,%f,%s|",     \
					t_node->item_name, *((double *)t_node->val), t_node->so_name);
			}
		} else {				
			snprintf(buf, sizeof(buf), "%s,*TYPE INVALID*,%s|",     \
				     t_node->item_name, t_node->so_name);
		}

		writen(cmd_ctxt->connfd, buf, strlen(buf));
	}
	
	writen(cmd_ctxt->connfd, "\r\n\r\n", 4);
}

/* list command */
static void
cmd_list(COMMAND_CONTEXT *cmd_ctxt)
{
	VAR_NODE *t_node;
	char buf[CMD_BUF_SIZE];
	
	SLIST_FOREACH(t_node, monitor_head, entries)
	{
		memset(buf, '\0', sizeof(buf));
		/* outputs follow the format below */
		snprintf(buf, sizeof(buf), "%s,%s|",   \
		    t_node->so_name, t_node->item_name);
		writen(cmd_ctxt->connfd, buf, strlen(buf));
	}
	
	writen(cmd_ctxt->connfd, "\r\n\r\n", 4);
}

/* help command */
static void
cmd_help(COMMAND_CONTEXT *cmd_ctxt)
{
	char a[] = ""
"Welcome!\r\n \r\n"
"  stat\t\tWatch the registered variables. Type Enter to quit.\r\n \r\n"
"\tstat item1,...,itemN [-e item1,...,itemN]\r\n\titem: items you want to monitor.\r\n"
"\t-e: excepet the items specified in the parameter.\r\n \r\n"
"  help\t\tFor help.\r\n"
"  list\t\tList the module loaded.\r\n"
"  stop\t\tTo stop the monitor service. And you CAN NOT connect again!\r\n"
;
	
	writen(cmd_ctxt->connfd, a, strlen(a));
}

/* stop command */
static void
cmd_stop(COMMAND_CONTEXT *cmd_ctxt)
{
	size_t i;
	int acceptfd;
	VAR_NODE *t_node;
	VAR_NODE *t_node_temp;
	
	/* close all fd and end the monitor */
	close(cmd_ctxt->listenfd);
	for (i = 0; i <= cmd_ctxt->all_connfd_len; i++) {
		acceptfd = cmd_ctxt->all_connfd[i].fd;
		if (acceptfd < 0) {
			continue;
		}
		close(acceptfd);
	}
	
	/* free memory */
	SLIST_FOREACH_SAFE(t_node, monitor_head, entries, t_node_temp) 
	{
        SLIST_REMOVE(monitor_head, t_node, entry, entries);
        free(t_node);
    }
	
	switch_monitor_run = 0;	
}

/* empty command */
static void
cmd_empty(COMMAND_CONTEXT *cmd_ctxt)
{
	writen(cmd_ctxt->connfd, "\r\n", 2);
	return;
}


/*********************************
 * monitor main part
 *********************************/


/* initialize the monitor and create thread */
void * 
monitor_init(void *handle)
{
	pthread_t tid;
    int listenfd;
    int r;
    uint16_t port;
	struct slisthead *headp;
 
	monitor_bind_ip[0] = 0;
    if (handle == NULL) {
		r = load_conf(NULL, "server", conf_int_array, conf_str_array); 
		if (r != 0 || !monitor_enable) {
			WLOG_ERR("[monitor] monitor is disabled.(monitor_enable=-1 or not specified.)");
			printf("Monitor is disabled. (monitor_enable=-1 or not specified.)\n");
			return NULL;
		}
		
		headp = (struct slisthead *)malloc(sizeof(struct slisthead));
		if (headp == NULL) {
			WLOG_ERR("[monitor] not enough memory to init monitor.");
			return NULL;
		} else {
			SLIST_INIT(headp);
			monitor_head = headp;
		}
    
		if (monitor_port < 0) {
			monitor_port = MNT_DEF_PORT;
			WLOG_ERR("[monitor] monitor port is specified incorrectly. Default port %ju is used.", (uintmax_t)MNT_DEF_PORT);
			printf("Monitor port is specified incorrectly. Default port %ju is used.\n", (uintmax_t)MNT_DEF_PORT);
		}
		
		if (monitor_bind_ip[0] == 0) {
			strlcpy(monitor_bind_ip, DEFAULT_BINDIP, CONF_ITEM_LEN+1);	
		}
	
		port = (uint16_t)monitor_port;
		listenfd = unix_listen(port, monitor_bind_ip);
        
		if (listenfd < 0) {
			WLOG_INFO("[monitor] bind to port %ju failed. ", (uintmax_t)port);
			printf("Monitor bind to port %ju failed.\n", (uintmax_t)port);
			return NULL;
		} else {
			mnt_fd = listenfd;
			WLOG_INFO("[monitor] Monitor OK. Now listen on port %ju.", (uintmax_t)port);
		}
	
		if (pthread_create(&tid, NULL, (void*)monitor_thread, NULL) != 0) {
			WLOG_ERR("creating monitor thread failed.");
			printf("Creating monitor thread failed.\n");
			return NULL;
		}
	} else {
		monitor_head = (struct slisthead *)handle;
	}	
	
	return (void *)monitor_head;
}	

/* monitor thread */
void * 
monitor_thread()
{	
    sigset_t new_set;

    sigemptyset(&new_set);
	sigaddset(&new_set, SIGPIPE);
	sigaddset(&new_set, SIGCHLD);
	sigaddset(&new_set, SIGQUIT);
	sigaddset(&new_set, SIGTRAP);
	sigaddset(&new_set, SIGHUP);
	sigaddset(&new_set, SIGTERM);
	sigaddset(&new_set, SIGINT);
	sigaddset(&new_set, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &new_set, NULL);

    monitor_start(mnt_fd);

	return NULL;
}

/* monitor starts here */
static void 
monitor_start(int listenfd)
{
	int i = 0;
    int nready;

	int connfd = -1;		
	int acceptfd = -1;	

	ssize_t	n;
	COMMAND_CONTEXT cmd_context;	/* transfer the command and its context */
	char ibuf[CMD_BUF_SIZE + 1];	/* input buffer */
	
    struct pollfd fd_array[MAX_OPEN_FD];
    fd_array[0].fd = listenfd;
    fd_array[0].events = POLLRDNORM;

    for (i = 1; i < MAX_OPEN_FD; i++) {
        fd_array[i].fd = -1;
    }

	for ( ; ; ) {
		nready = poll(fd_array, MAX_OPEN_FD, -1);

		/* test listen FD. readable */
		if (fd_array[0].revents & POLLRDNORM) {	
			connfd = accept_new_client(listenfd, fd_array);	

			if (connfd < 0) {
				continue;
			} else {
                show_welcome(connfd);
                if (--nready <= 0)
                    continue;
            }			
		}	
		
		for (i = 1; i < MAX_OPEN_FD; i++)		/* all connect clients */	
		{	
			memset(ibuf, '\0', CMD_BUF_SIZE + 1);		/* erase the buffer */
			acceptfd = fd_array[i].fd;		
			if (acceptfd < 0) {
				continue;
			}

			/* when readable of connection FD */
			if (fd_array[i].revents & (POLLRDNORM | POLLERR)) {
				n = readline(acceptfd, ibuf, CMD_BUF_SIZE);	
				if (n < 0) {
					if (errno == ECONNRESET) {
                        close(connfd);
                        fd_array[i].fd = -1;
                    } else {
                        WLOG_ERR("read error.");
                    }
                } else if (n == 0) {
                    close(connfd);
                    fd_array[i].fd = -1;
                } else	{
					/* copy the relative value to struct */
					cmd_context.command = ibuf;
					cmd_context.listenfd = listenfd;
					cmd_context.connfd = acceptfd;
					cmd_context.all_connfd = fd_array;
					cmd_context.all_connfd_len = MAX_OPEN_FD;
					
					//WLOG_INFO("[monitor] command received: %s; from fd: %d", ibuf, acceptfd);
					
					/* 
                     * this function is	used to find out the right
					 * function for processing cases via analysing the command 
                     */		
					command_distribute(&cmd_context);	

				}
			}
		}
		
		/* when the switch off, thread function returns */
		if (switch_monitor_run == 0) {
			return;
		}
	}
}


/* accept new client */
static int 
accept_new_client(int listenfd, struct pollfd *fd_array)
{
	int i;
	int connfd;
	
	connfd = unix_accept(listenfd);
	
	if (connfd < 0) {
		WLOG_INFO("[monitor] new connection accept error.");
		return CSF_ERR;
	}

	for (i = 0; i < MAX_OPEN_FD; i++) {
		if (fd_array[i].fd < 0) {
			fd_array[i].fd = connfd;
			fd_array[i].events = POLLRDNORM;
			WLOG_INFO("[monitor] new connfd: %d saved in fd_array: %d of %d.", connfd, i, MAX_OPEN_FD - 1);
			break;
		}
	}
	
	/* reach the max client limit */ 
	if (i == MAX_OPEN_FD) {
		close(connfd);
		WLOG_INFO("[monitor] max client limit reached. fd: %s is refused.", connfd);
		return CSF_ERR;
	}								
	return connfd;
}

extern char *ver;
/* show welcome message */
static void 
show_welcome(int io)
{
	time_t timeticks;
	char a[] = "CSF Monitor. Type \"help\" for help.\r\n"
"Press \"CTRL+C\" to quit.\r\n";

	char msg[MAX_STR_LEN + 1];
	timeticks = time(NULL);
	snprintf(msg, MAX_STR_LEN, "%s%sCSF start time: %s\r\n", a, ver, ctime(&timeticks));
	writen(io, msg, strlen(msg));
}


/*********************************
 * register part
 *********************************/

/* register function */
void 
register_stat_int(const char *so_name, const char *item_name, int option, void *val)
{
	VAR_NODE *p_node;   /* no use until now. reserved. */
	
    if (!monitor_enable) {
		WLOG_WARNING("[monitor] Monitor is not enable but used.");
		return;
	}
    
    if (option >= 0 && item_name != NULL && val != NULL && so_name != NULL) {
	    if ((p_node = add_node(item_name, val, so_name, option)) == NULL) {
            return;
	    }
    } else if (option == -1) {
        remove_node(val);
    }
}

/* 
 * add a node .
 * input: the pointer of head node,item name,item value.
 * return: NULL when failed; The new malloced struct's pointer when SUCC. 
 */
static VAR_NODE *
add_node(const char *item_name, void *val, const char *so_name, int option)
{
	VAR_NODE *p_node;
	
	p_node = (VAR_NODE*)malloc(sizeof(VAR_NODE));
	
	if (p_node == NULL) {
	    return NULL;
	}
	
	memset(p_node->item_name, '\0', MAX_FIELD_LENGTH);
	memset(p_node->so_name, '\0', MAX_FIELD_LENGTH);
	
	strlcpy(p_node->item_name, item_name, MAX_FIELD_LENGTH);
	strlcpy(p_node->so_name, so_name, MAX_FIELD_LENGTH);
	p_node->val = val;
	p_node->option = option;
	
	SLIST_INSERT_HEAD(monitor_head, p_node, entries);

    /*
	WLOG_INFO("[monitor] new item registered: %s->%s; value addr: %p; type: %d", so_name, item_name, val, option);
	*/
	
	return p_node;
}


/* remove a node from the list */
void
remove_node(void *val)
{
	VAR_NODE *t_node;
    VAR_NODE *t_node_temp;
	
    SLIST_FOREACH_SAFE(t_node, monitor_head, entries, t_node_temp) 
	{
        if (t_node->val == val || val == NULL) {
            SLIST_REMOVE(monitor_head, t_node, entry, entries);
            WLOG_INFO("[monitor] all items are removed.");
            free(t_node);
        }
    }
}


/*********************************
 * socket part
 *********************************/


int 
unix_accept(int listenfd)
{
	int connfd;
	int len;
	struct sockaddr_in cliaddr;
	
	len = sizeof(cliaddr);
	connfd = accept(listenfd, (struct sockaddr *)&cliaddr, (socklen_t *)&len);
	
	return connfd;
}

int 
unix_listen(uint16_t port, char *ip)
{
	struct sockaddr_in sin;
	int s;

	/* Create unix domain socket. */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		WLOG_INFO("[monitor] socket error.");
		return CSF_ERR;
	}

	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr(ip);

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(s);
		WLOG_INFO("[monitor] error occured when bind to port %ju.", (uintmax_t)port);
		return CSF_ERR;
	}

	if (listen(s, 5) < 0) {
		close(s);
		WLOG_INFO("[monitor] error occured when listen to port %ju.", (uintmax_t)port);
		return CSF_ERR;
	}
	
	return s;
}

/* read and write */
ssize_t	
readn(int fd, void *vptr, size_t n)
{
	size_t	nleft;
	ssize_t	nread;
	char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)
				nread = 0;		/* and call read() again */
			else
				return CSF_ERR;
		} else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

ssize_t	
writen(int fd, const void *vptr, size_t n)
{
	size_t		nleft;
	ssize_t		nwritten;
	const char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
			if (errno == EINTR)
				nwritten = 0;		/* and call write() again */
			else
				return CSF_ERR;			/* error */
		}

		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}

ssize_t 
readline(int fd, void *vptr, size_t maxlen)
{
	ssize_t	rc;
	size_t n;
	char	c, *ptr;

	ptr = vptr;
	for (n = 1; n < maxlen; n++) {
		if ( (rc = read(fd, &c, 1)) == 1) {
			*ptr++ = c;
			if (c == '\n' || c == 3)
				break;
		} else if (rc == 0) {
			if (n == 1)
				return CSF_OK;	/* EOF, no data read */
			else
				break;		/* EOF, some data was read */
		} else
			return CSF_ERR;	/* error */
	}

	*ptr = 0;
	return(n);
}

