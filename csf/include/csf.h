#ifndef _CSF_CSF_H
#define _CSF_CSF_H

#include "confparser.h"
#include "comm_proto.h"
#include "ver_cntl.h"

#define DEFAULT_SERVER_PORT	24
#define DEFAULT_TIMEOUT		10
#define DEFAULT_PIPELINE_DELAY	0
#define DEFAULT_PIPELINE_ACTION	1
#define DEFAULT_REQUEST_TIMEOUT	0
#define DEFAULT_PIPELINE_QUEUE_SIZE	0
#define DEFAULT_DAEMONIZE	1
#define DEFAULT_MONITOR_ENABLE	1
#define DEFAULT_MONITOR_PORT	22222
#define DEFAULT_SERVER_PROTOCOL "tcp"
#define DEFAULT_PROTOCOL_MODULE ""
#define DEFAULT_USER "root"
#define DEFAULT_GROUP "wheel"
#define DEFAULT_BIND_IP "0.0.0.0"
#define DEFAULT_MOD_DIR "./"
#define DEFALUT_MOD_PATH "./"
#define DEFAULT_MONITOR_BIND_IP "0.0.0.0"

typedef struct csf_conf {
	COMM_PROTO_OPS *cp_ops;
	int	server_port;
	int	server_timeout;
	int	pipeline_queue_size;
	int	daemonize;
	int request_timeout;
	int monitor_enable;
	int monitor_port;
	char	server_protocol[CONF_ITEM_LEN + 1];
	char	protocol_module[CONF_ITEM_LEN + 1];
	char	user[CONF_ITEM_LEN + 1];
	char	group[CONF_ITEM_LEN + 1];
	char	log_ident[CONF_ITEM_LEN + 1];
	char	bind_ip[CONF_ITEM_LEN + 1];
	char	mod_dir[CONF_ITEM_LEN + 1];
	char	mod_path[CONF_ITEM_LEN + 1];
	char	monitor_bind_ip[CONF_ITEM_LEN + 1];
} CSF_CONF;

typedef struct comm_handle
{
	void *mntp;
	void *logp;
	void *stagep;
	void *sribp;	
} COMM_HANDLE;


#endif

