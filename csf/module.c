#include <syslog.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/param.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "log.h"
#include "mod_conf.h"
#include "confparser.h"
#include "server.h"
//#include "common.h"
#include "utils.h"
#include "protocol.h"
#include "mempool.h"
#include "pipeline.h"
#include "monitor.h"

extern void set_protocol_init(_PROTOCOL_INIT *);

int
mod_config_init (CSF_CONF *, struct mod_config_queue *,
	VCB *, COMM_HANDLE *);

static int mod_conf_threadnum = DEFAULT_THREAD_NUMBER;
static int mod_conf_stage_id = DEFAULT_THREAD_STAGE_ID;
static int mod_conf_pipeline_id = DEFAULT_PIPELINE_ID;
static int mod_conf_pipeline_delay = DEFAULT_PIPELINE_DELAY;


static CONF_INT_CONFIG mod_conf_int_array[] = {	
	{"threads", &mod_conf_threadnum},	
	{"stage_id", &mod_conf_stage_id},	
	{"pipeline_id",	&mod_conf_pipeline_id},
	{"delay",	&mod_conf_pipeline_delay},	
	{0, 0}
};

static int
load_protocol_mod(char *dir, char *mod_name)
{
	char buf[2 * CONF_ITEM_LEN + 1];
	void *handle = NULL;
	_PROTOCOL_INIT *proto_init;

	snprintf(buf, 2 * CONF_ITEM_LEN, "%s/%s", dir, mod_name);
	handle = dlopen(buf, RTLD_LAZY);
	if(handle == NULL) {
		PRINT("error:%s", dlerror());
		return (CSF_ERR);
	}
	
	proto_init = (_PROTOCOL_INIT *)dlsym(handle, "_protocol_init");
	if (proto_init == NULL) {
		PRINT("error:%s", dlerror());
		return (CSF_ERR);
	}
	set_protocol_init(proto_init);
	PRINT("PROTOCOL MODULE: %s", mod_name);

	return (CSF_OK); 
}


int
mod_config_init (
CSF_CONF *cfgp,
struct mod_config_queue *mcqp,
VCB *vcbp,
COMM_HANDLE *comm_handle)
{
	void *handle = NULL;
	struct mod_config *msp = NULL;
	struct mod_config *msp1 = NULL;
	int i;
	int nsec;
	char *secname;
	char buf[2 * CONF_ITEM_LEN + 1];
	int seclen;
	_MOD_INIT *mip;
	int flag = 0;
	int retval = 0;
	dictionary *conf;
	MOD_PARA mpp;

	conf = open_conf_file(NULL);
	if (conf==NULL) {
		PRINT("cannot parse configure file.");
		return (CSF_ERR);
	}

	load_protocol_mod(cfgp->mod_dir, cfgp->protocol_module);

	nsec = iniparser_getnsec(conf);
	if (nsec < 1) {
		close_conf_file(conf);
		return (CSF_OK);
	}
	
	for (i = 0; i < nsec; i++) {
		secname = iniparser_getsecname(conf, i) ;
		seclen	= (int)strlen(secname);

		/* Any section named "*.so" is the extension mod. */
		if ((strncmp(secname, "server", seclen) != 0) &&
			(strncmp(secname, cfgp->protocol_module, seclen) != 0)) {

			/* And the rest, is the mod of pipeline stages */
			snprintf(buf, 2 * CONF_ITEM_LEN, "%s/%s", cfgp->mod_dir, secname);
			handle = dlopen(buf, RTLD_LAZY);
			if (handle == NULL) {
				PRINT("error:%s", dlerror());
				close_conf_file(conf);
				return (CSF_ERR);
			}

			/* We load the mod_init function */
			mip = (_MOD_INIT *)dlsym(handle, "_mod_init");
			
			if (mip == NULL) {
				PRINT("error:%s", dlerror());
			}
			
			msp = (struct mod_config*)calloc(1, sizeof(struct mod_config));
			if (msp == NULL) {
				dlclose(handle);
				PRINT("Not enough memory.");
				close_conf_file(conf);
				return (CSF_ERR);
			}

			/* save the mod information into msp */
			retval = load_conf(NULL, secname, mod_conf_int_array, NULL);	
			if (retval != 0) {		
				WLOG_ERR("load configure failed!");		
				return MOD_CONF_ERR;	
			}	
			msp->stage_id = mod_conf_stage_id;	
			msp->thread_num = mod_conf_threadnum;	
			msp->pipeline_id = mod_conf_pipeline_id;
			msp->delay = mod_conf_pipeline_delay;

			mpp.chp = comm_handle;
			mpp.mcp = msp;
			mpp.vcbp = vcbp;
			
			retval = mip(secname, &mpp);
			if (retval < 0) {
				
				switch (retval) {
					case MOD_LIB_ERR : 
						PRINT("MOD(name: %s, id: %d) init failed.",
							secname, msp->stage_id);
						WLOG_ERR("MOD(name: %s, id: %d) init failed.",
							secname, msp->stage_id);
						break;
					case MOD_VER_ERR :
						PRINT("MOD(name: %s, id: %d) version mismatch.",
							secname, msp->stage_id);
						WLOG_ERR("MOD(name: %s, id: %d) version mismatch.",
							secname, msp->stage_id);
						break;
					case MOD_CONF_ERR:
						PRINT("MOD(name: %s, id: %d) can not load configure file.",
							secname, msp->stage_id);
						WLOG_ERR("MOD(name: %s, id: %d) can not load configure file.",
							secname, msp->stage_id);
						break;
					default :
						PRINT("MOD(name: %s, id: %d) unknown error.",
							secname, msp->stage_id);
						WLOG_ERR("MOD(name: %s, id: %d) unknown error.", secname,
							msp->stage_id);
						break;
				}
				
				free(msp);
				dlclose(handle);
				msp = NULL;
				close_conf_file(conf);
				return CSF_ERR;
			}

			if (msp->pipeline_id >= PIPELINE_SIZE){
				free(msp);
				dlclose(handle);
				msp = NULL;
				PRINT("PIPELINE %d IS IGNORED. The id is larger than the limit [%d].", 
						msp->pipeline_id, PIPELINE_SIZE);
				continue;
			}

			strlcpy(msp->mod_name, secname, MAX_MOD_NAME_LEN);

			/* 
			 * _mod_init() return a struct of mod_init_t which include
			 * the config of. We insert it to mod queue for config.
			 * We sort pipeline_id first and stage_id second.
			 * The resule is:
			 * 1,1->1,2->1,n->2,1->2,2->2,n->n,1->n,2->n,n...
			 */
			if (TAILQ_EMPTY(mcqp)) {
				TAILQ_INSERT_TAIL(mcqp, msp, mod_entries);
				msp = NULL;
				continue;
			} else {
				TAILQ_FOREACH(msp1, mcqp, mod_entries) {
					if (msp1->pipeline_id > msp->pipeline_id) {
						TAILQ_INSERT_BEFORE(msp1, msp, mod_entries);
						flag = 1;
						break;
					}
				}
				if (!flag) {
					msp1 = TAILQ_LAST(mcqp, mod_config_queue);
					if (msp1 != NULL && msp1->pipeline_id == msp->pipeline_id) {
						if (msp1->stage_id > msp->stage_id)
							TAILQ_INSERT_BEFORE(msp1, msp, mod_entries);
						else 
							TAILQ_INSERT_TAIL(mcqp, msp, mod_entries);
					} else {
						TAILQ_INSERT_TAIL(mcqp, msp, mod_entries);
					}
				}
				flag = 0;
				msp = NULL;
			}
			
		}
	}
	close_conf_file(conf);
	return (CSF_OK);
}

