/**
 * CSF module API lib
 * Ver 4.0.0
 *
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include "libmod.h"
#include "confparser.h"
#include "log.h"
#include "monitor.h"

int _mod_init(char *, MOD_PARA *);
int mod_init(char *);

static pthread_once_t init_done = PTHREAD_ONCE_INIT;
static pthread_key_t pthread_key;

static void pthr_key_create(void); 
static void pthr_key_create() 
{	
	pthread_key_create(&pthread_key, NULL);
}

static REQUEST_INIT **ripp = NULL;
static REQUEST_HANDLER **rhpp = NULL;
static REQUEST_DEINIT **rdpp = NULL;


/***************************************************
 * functions 
 ***************************************************/
void
set_request_init(REQUEST_INIT *rip)
{
	if (ripp != NULL)
		*ripp = rip;
}

void 
set_request_handler(REQUEST_HANDLER *rhp)
{
	if (rhpp != NULL)
		*rhpp = rhp;
}

void
set_request_deinit(REQUEST_DEINIT *rdp)
{
	if (rdpp != NULL)
		*rdpp = rdp;
}


/***************************************************
 * CSF IO operations API
 ***************************************************/
 
int 
send_file(CONN_INFO *cip, int fd, size_t size, off_t *offset, off_t *sent)
{
	if (cip == NULL)
		return MOD_LIB_ERR;
		
	return (cip->cp_ops->send_file)(cip, fd, size, offset, sent);
}
 
 
ssize_t 
writev_back(CONN_INFO *cip, const struct iovec *vector, 
			uint16_t vector_len, int timeout)
{
	if (cip == NULL)
		return MOD_LIB_ERR;
		
	return (cip->cp_ops->writev_back)(cip, vector, vector_len, timeout);
}

ssize_t 
write_back(CONN_INFO *cip, const char *buf, size_t len, int timeout)
{
	if (cip == NULL)
		return MOD_LIB_ERR;
		
	return (cip->cp_ops->write_back)(cip, buf, len, timeout);
}

void 
set_thread_data(const void *data)
{
	pthread_setspecific(pthread_key, data);
    
}

void * 
get_thread_data()
{
	return pthread_getspecific(pthread_key);
}


/***************************************************
 * CSF module entry
 ***************************************************/
 
 int
_mod_init(char *mod_name, MOD_PARA *mpp)
{
	/* check whether the mod version match the csf core version */
	if (CORE_VER_REQUIRE > mpp->vcbp->lowest_ver)
		return MOD_VER_ERR;

	logger_init(mpp->chp->logp, NULL, 0, 0);
	stage_init(mpp->chp->stagep); 
	monitor_init(mpp->chp->mntp);
	
	pthread_once(&init_done, pthr_key_create);
    pthread_setspecific(pthread_key, NULL);
	
	ripp = &(mpp->mcp->request_init);
	rdpp = &(mpp->mcp->request_deinit);
	rhpp = &(mpp->mcp->request_handler);
	
    return mod_init(mod_name);
}


