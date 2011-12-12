/*-
 * Copyright (c) 2007-2009 SINA Corporation, All Rights Reserved.
 *  Authors:
 *      Zhu Yan <zhuyan@staff.sina.com.cn>
 *
 * Main Entry
 */

#include <syslog.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/param.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "log.h"
#include "mod_conf.h"
#include "confparser.h"
#include "server.h"
//#include "common.h"
#include "protocol.h"
#include "mempool.h"
#include "pipeline_def.h"
#include "monitor.h"
#include "ver_cntl.h"
#include "csf.h"
#include "comm_proto.h"
#include "utils.h"

VCB gvcb = {0x00400000};

CSF_CONF main_conf;
COMM_HANDLE comm_handle;
COMM_HANDLE *g_comm_handle = &comm_handle;
const char *ver = "PANTAO 1.0-alpha1 Build-Date: " __DATE__ " " __TIME__ "\n";
static char exec_path[PATH_MAX + 1];

static struct mod_config_queue mod_queue_head =
	TAILQ_HEAD_INITIALIZER(mod_queue_head);

static struct	conf_int_config conf_int_array[] = {
	{"port",	&(main_conf.server_port)},
	{"timeout",	&(main_conf.server_timeout)},
	{"request_timeout",	&(main_conf.request_timeout)},
	{"pipeline_queue_size",	&(main_conf.pipeline_queue_size)},
	{"daemonize",	&(main_conf.daemonize)},
	{"monitor_enable", &(main_conf.monitor_enable)},
	{"monitor_port", &(main_conf.monitor_port)},
	{0, 0}
};

static struct	conf_str_config conf_str_array[] = {
	{"protocol", main_conf.server_protocol},
	{"protocol_module",	main_conf.protocol_module},
	{"user",	main_conf.user},
	{"group",	main_conf.group},
	{"log_ident", main_conf.log_ident},
	{"bind_ip", main_conf.bind_ip},
	{"mod_dir", main_conf.mod_dir},
	{"monitor_bind_ip", main_conf.monitor_bind_ip},
	{0, 0}
};

static uint32_t maxfd = 10240;
int graceful_restart = 0;

extern COMM_PROTO_OPS tcp_ops;
extern COMM_PROTO_OPS udp_ops;

/* start it from 1, keep the 0 for others */
long clock_off = 1;

static COMM_PROTO_OPS *csf_comm_proto[] = {
	&tcp_ops,
	&udp_ops,
	NULL
};

extern int app_proto_init(const char *, COMM_PROTO_OPS *, VCB *, COMM_HANDLE *);




static COMM_PROTO_OPS *
get_comm_proto(char *proto_name)
{
	COMM_PROTO_OPS *ops;
	int i;

	ops = csf_comm_proto[0];
	for (i = 0; ops != NULL; ops = csf_comm_proto[++i])
		if (strncasecmp(proto_name, ops->proto_name, CONF_ITEM_LEN) == 0)
			return ops;
	return NULL;
}

static void
csf_conf_init(CSF_CONF *conf)
{
	conf->cp_ops = NULL;
	conf->server_port = DEFAULT_SERVER_PORT;
	conf->server_timeout = DEFAULT_TIMEOUT;
	conf->request_timeout = DEFAULT_REQUEST_TIMEOUT;
	conf->pipeline_queue_size = DEFAULT_PIPELINE_QUEUE_SIZE;
	conf->daemonize = DEFAULT_DAEMONIZE;
	conf->monitor_enable = DEFAULT_MONITOR_ENABLE;
	conf->monitor_port = DEFAULT_MONITOR_PORT;

	strlcpy(conf->server_protocol, DEFAULT_SERVER_PROTOCOL, CONF_ITEM_LEN + 1);
	strlcpy(conf->protocol_module, DEFAULT_PROTOCOL_MODULE, CONF_ITEM_LEN + 1);
	strlcpy(conf->user, DEFAULT_USER, CONF_ITEM_LEN + 1);
	strlcpy(conf->group, DEFAULT_GROUP, CONF_ITEM_LEN);
	strlcpy(conf->log_ident, DEFAULT_WLOG_IDENT, CONF_ITEM_LEN + 1);
	strlcpy(conf->bind_ip, DEFAULT_BIND_IP, CONF_ITEM_LEN + 1);
	strlcpy(conf->mod_dir, DEFAULT_MOD_DIR, CONF_ITEM_LEN + 1);
	strlcpy(conf->monitor_bind_ip, DEFAULT_MONITOR_BIND_IP, CONF_ITEM_LEN + 1);
}

static void
set_maxfd(void)
{
	struct rlimit rl;

	if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
		CRIT("can't get file limit");
	} else {
		rl.rlim_cur = maxfd;
		if (rl.rlim_cur > rl.rlim_max) {
			rl.rlim_cur = rl.rlim_max;
			maxfd = rl.rlim_cur;
		}
		if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
			CRIT("setrlimit() error! ");
		}
	}
}

extern int
mod_config_init (char *, struct mod_config_queue *, 
	VCB *, COMM_HANDLE *);

static int
config_init(void)
{
	dictionary *conf;

	conf = open_conf_file(NULL);
	if (conf==NULL) {
		PRINT("cannot parse configure file.");
		return (CSF_ERR);
	}

	parse_conf_file(conf, "server", conf_int_array, conf_str_array);

	main_conf.cp_ops = get_comm_proto(main_conf.server_protocol);

	if (main_conf.cp_ops == NULL)
		CRIT("UNKNOWN COMMUNICATION PROTOCOL!");
	    
	close_conf_file(conf);

	return (CSF_OK);
}


static void
output_config(void)
{
	printf("maximum file descriptors: %d\n", maxfd);
	printf(ver);
}


static void 
show_help (void) {

	printf("%s" \
	"usage:\n" \
	" -f <name>  filename of the config-file\n" \
	" -v         show version\n" \
	" -h         show this help\n" \
	"\n",
	ver);
}


/* set the working directory of CSF */
static int
set_working_dir(const char *_exec_path)
{
	int i;
	int res;
	int exec_dirlen;
	char exec_dir[PATH_MAX + 1];

	
	/* get the real path of the programme(file name is not included) */
	exec_dirlen = strlen(_exec_path);
	memcpy(exec_dir, _exec_path, exec_dirlen);
	
	for (i = exec_dirlen - 1; i >= 0; i--) {
		if (exec_dir[i] == '/') {
			exec_dir[i] = '\0';
			break;
		}
	}
	
	/* set the current working path */
	res = chdir(exec_dir);
	if (res != 0) {
		printf("Working directory set to: %s failed.\n", exec_dir);
        perror("Working dir:");
	}
	
	return res;
}

extern void kick_request_queue(void);
extern void broadcast_signal_to_allqueue(void);
extern uint64_t pipeline_queue_len;

static int
HUP_process(void)
{
/*
 * 1. shutdown port
 * 2. deny submit_request
 * 3. kick the tmp_request_head
 * 4. waiting for all request done
 * 5. broadcast_signal_to_allqueue 
 * 6. request_deinit
 * 7. exit
 */
	COMM_PROTO_OPS *ops;
	
 	graceful_restart = 1;

	ops = main_conf.cp_ops;

 	if (ops != NULL && ops->port_close != NULL)
		ops->port_close();

	kick_request_queue();

	if (pipeline_queue_len == 0)
		broadcast_signal_to_allqueue();
		
	return (0);
}


static void *
signals_handler(void *arg) 
{
	CSF_UNUSED_ARG(arg);
	siginfo_t info;	
	struct timespec timeout;
	int err;
	int signo;

	sigset_t        mask;
	sigemptyset (&mask);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
	timeout.tv_sec = 1;
	timeout.tv_nsec = 0;

	for (;;) {
		err = sigtimedwait(&mask, &info, &timeout);
		if (err < 0) {
			if (errno == EAGAIN) {
				clock_off++;	
				continue;
			} else if (errno == EINTR) {
				continue;
			} else {
				WLOG_INFO("sigtimedwait failed errno: %d", errno);
				exit(1);
			}
		}
		signo = info.si_signo;

		if (signo == SIGHUP) {
			WLOG_INFO("got SIGHUP");
			HUP_process();
		} else if (signo == SIGTERM || signo == SIGINT) {
			WLOG_INFO("got singal %d, exiting", signo);
			exit(0);
		} else {
			WLOG_INFO("unexpected signal %d\n", info.si_signo);
		}
	}
	
	return(0);

}

static void
set_signals(void)
{
	struct sigaction	act;
	pthread_t	tid;
	int err;
	sigset_t        mask;
	pthread_attr_t	pattr;
	struct sched_param psched;

	pthread_attr_init(&pattr);
	pthread_attr_setscope(&pattr, PTHREAD_SCOPE_SYSTEM);
	psched.sched_priority = 2;
	pthread_attr_setschedparam(&pattr, &psched);

	/* Signals it handles*/
	sigemptyset (&mask);
	memset(&act, 0, sizeof(act));
	
	/* Signals it handles */
    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGHUP, &act, NULL) < 0)
            PRINT("can't restore SIGHUP default");
    sigfillset(&mask);
    if ((err = pthread_sigmask(SIG_BLOCK, &mask, NULL)) != 0)
		PRINT("SIG_BLOCK error: %d", err);

    /* Create a thread to handle SIGHUP and SIGTERM. */
    err = pthread_create(&tid, &pattr, signals_handler, 0);
    if (err != 0) {
		PRINT("can't create thread");
    }
	pthread_detach(tid);
}

/* init the CSF modules */
static int 
csf_init(void)
{
    int rc;

	csf_conf_init(&main_conf);

	if (config_init() < 0)
		return CSF_ERR;
	else
		output_config();	

	/* start log */
	if (main_conf.log_ident[0] == '\0')
		comm_handle.logp = logger_init(NULL, DEFAULT_WLOG_IDENT,
		LOG_CONS|LOG_PID, LOG_LOCAL6);
	else
		comm_handle.logp = logger_init(NULL, main_conf.log_ident,
		LOG_CONS|LOG_PID, LOG_LOCAL6);
	
//	comm_handle.mntp = monitor_init(NULL, main_conf.monitor_bind_ip, main_conf.monitor_port);
	comm_handle.sribp = request_info_init(&main_conf);

	if (main_conf.monitor_enable) {
//		if (monitor_start() < 0) {
//			WLOG_ERR("Monitor start failed.");
//			PRINT("Monitor start failed.");
//		}
	}

	if (main_conf.daemonize)
		daemonize(1, 1);

	set_maxfd();	
	
	/*
	  * We prealloc the mempool
	  * XXX We use pipeline_queue_size may not be suited?
	  */
	mp_init(main_conf.pipeline_queue_size / 10, 
		main_conf.pipeline_queue_size / 2);

	rc = mod_config_init(&main_conf, 
		&mod_queue_head, &gvcb, &comm_handle);
    if (rc != CSF_OK) {
        return (CSF_ERR);
    }

    /* 
     * init pipeline module
     * (call the initialization function defined at protocol source file)
     */
    if (!TAILQ_EMPTY(&mod_queue_head)) {
		rc = pipeline_init(&mod_queue_head, 
				main_conf.pipeline_queue_size,
				main_conf.request_timeout);
		if (rc != PIPELINE_INIT_OK) {
	        WLOG_ERR("Can not init pipeline.");
	        PRINT("Can not init pipeline.");
	        return (CSF_ERR);
    	}
    }

	/*init protocol module*/
	rc = app_proto_init(main_conf.protocol_module,
		main_conf.cp_ops, &gvcb, &comm_handle);
	if (rc < 0)
		return (CSF_ERR);
		
    return (CSF_OK);
}


/* save current PID file to /var/run/<proc name>.pid */
static void
save_pid(const char *_exec_name)
{
	int i;
	int exec_namelen;
	int last_slash = 0;
	FILE *pid_f;
	pid_t pid;
	char pid_str[PATH_MAX + 1];
	char *app_name = NULL;
	char exec_appname[PATH_MAX + 1];
	char pid_file_path[PATH_MAX + 1];
	
	/* set empty */
	memset(pid_str, 0, PATH_MAX + 1);
	memset(exec_appname, 0, PATH_MAX + 1);
	memset(pid_file_path, 0, PATH_MAX + 1);
	
	/* get the real path of the programme(file name is not included) */
	exec_namelen = strlen(_exec_name);
	strlcpy(exec_appname, _exec_name, PATH_MAX + 1);
	
	/* get the app name from arg[0] */
	app_name = exec_appname + last_slash;

	for (i = 0; i < exec_namelen; i++) {
		if (exec_appname[i] == '/') {
			last_slash = i + 1;
			app_name = exec_appname + last_slash;
		}
			
		if (exec_appname[i] == ' ') {
			exec_appname[i] = '\0';
			break;
		}
	}
	
	
	/* open file and write the PID to it */
	if (app_name != NULL) {
		snprintf(pid_file_path, PATH_MAX, "/var/run/%s.pid", app_name);
		
		pid_f = fopen(pid_file_path, "w");
		if (pid_f == NULL) {
			PRINT("save_pid: can not create %s.", pid_file_path);
            perror("save_pid");
			return;
		}
		
		pid = getpid();
		snprintf(pid_str, PATH_MAX, "%u", (unsigned int)pid);
		
		if (fwrite(pid_str, strlen(pid_str), 1, pid_f) < 1) {
			PRINT("save_pid: can not write pid to %s.", pid_file_path);
            perror("save_pid");
			return;
		}
		
		fclose(pid_f);
	}
	else {
		PRINT("save_pid: can not get application name.");
	}
}

int 
main(int argc, char **argv)
{
	int o;
	
	printf("\nCSF[%d] starting...\n", (int)getpid());

	realpath(argv[0], exec_path);
	if (set_working_dir(exec_path) != 0) {
		PRINT("Can not set the working directory.");
		return (2);
	}
	set_conf_file(CONF_FILE);

	while(-1 != (o = getopt(argc, argv, "f:hv"))) {
		switch(o) {
		case 'f':
			set_conf_file(optarg);
			break;

		case 'v': output_config(); return 0;
		case 'h': show_help(); return 0;
		default:
			break;
		}

	}

    /* init each modules */
	if (csf_init() < 0) {
        PRINT("CSF init failed. Exited.");
        return (2);
    }
	set_signals();
	save_pid(argv[0]);	

    /* starts the server */

    server_init(&main_conf);

    WLOG_ERR("Fatal Error, SERVER DOWN!");
	logger_deinit();
    PRINT("Fatal Error, SERVER DOWN!");
    
	return (2);
}

