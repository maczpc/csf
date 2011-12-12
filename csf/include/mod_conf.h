#ifndef _CSF_PLUGIN_H
#define _CSF_PLUGIN_H

#include "queue.h"
#include "pipeline.h"
#include "ver_cntl.h"

#define CONF_FILE			"csf.conf"
#define MAX_MOD_NAME_LEN 	64

#define MOD_LIB_OK		0
#define MOD_LIB_ERR		-1
#define MOD_VER_ERR		-2
#define MOD_CONF_ERR	-3
#define MOD_INIT_OK		0
#define MOD_INIT_ERR	-1

typedef struct mod_config {
	TAILQ_ENTRY(mod_config) mod_entries;
	uint32_t		pipeline_id;
	uint32_t		delay;
	int				stage_id;
	int				thread_num;
	char			mod_name[MAX_MOD_NAME_LEN];
	REQUEST_INIT	*request_init;
	REQUEST_DEINIT	*request_deinit;
	REQUEST_HANDLER	*request_handler;
} MOD_CONFIG;

typedef struct mod_parameter{	
	VCB *vcbp;	
	MOD_CONFIG *mcp;	
	COMM_HANDLE *chp;
} MOD_PARA;

typedef int _MOD_INIT(char *, MOD_PARA *);
TAILQ_HEAD(mod_config_queue, mod_config);

#endif

