/**
 * CSF protocol API lib
 * Ver 4.0.0
 *
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include "libprotocol.h"
#include "log.h"
#include "monitor.h"
#include "utils.h"

#define PROTO_NAME_LEN	127

static char protocol_name[PROTO_NAME_LEN + 1];
static PROTO_CONFIG *g_pcp = NULL;


int protocol_init(char *proto_name);
int _protocol_init(const char *, PROTO_PARA *);


static pthread_once_t init_done = PTHREAD_ONCE_INIT;
static pthread_key_t pthread_key;

static void pthr_key_create(void); 
static void pthr_key_create() 
{	
	pthread_key_create(&pthread_key, NULL);
}


void
set_local_entry(LOCAL_ENTRY *entry)
{
	g_pcp->local_entry = entry;
}

void
set_protocol_session_start(PROTOCOL_SESSION_START *start)
{
	g_pcp->protocol_session_start = start;
}

void
set_protocol_session_entry(PROTOCOL_SESSION_ENTRY *entry)
{
	g_pcp->protocol_session_entry = entry;
}

void
set_protocol_session_end(PROTOCOL_SESSION_END *end)
{
	g_pcp->protocol_session_end = end;
}

/* ======== devider ======= */

int
send_back(CONN_INFO *cip, const void *buf, const int len)
{
	if (cip == NULL)
		return -1;
		
	return (cip->cp_ops->send_back)	\
			(cip, buf, len);
}
  
 
char *
ip2str(CONN_INFO *cip, char *ipstr, size_t len)
{
	if (inet_ntop(AF_INET, &(cip->addr.sin_addr), ipstr, len) == NULL)
		return (NULL);
	return ipstr;
}


static inline void set_protocol_name(const char *);
static inline char *get_protocol_name(void);

static inline void
set_protocol_name(const char *name)
{
	if (name != NULL)
		strlcpy(protocol_name, name, PROTO_NAME_LEN + 1);
	else
		WLOG_ERR("protocol name is NULL!");
}

static inline char *
get_protocol_name(void)
{
	return (protocol_name);
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
 * CSF protocol entry
 ***************************************************/

int
_protocol_init(const char *prot_name, PROTO_PARA *ppp)
{
	/* check whether the protocol version match the csf core version */
	if (CORE_VER_REQUIRE > ppp->vcbp->lowest_ver)
		return PROTO_VER_ERR;
		
	logger_init(ppp->chp->logp, NULL, 0, 0);
	mp_init(20, 100);   
	monitor_init(ppp->chp->mntp);
	submit_request_init(ppp->chp->sribp);
	
	g_pcp = ppp->pcp;

	set_protocol_name(prot_name);
	
    pthread_once(&init_done, pthr_key_create);
    pthread_setspecific(pthread_key, NULL);
	
	if (protocol_init(get_protocol_name()) < 0)
        return PROTO_INIT_ERR;
	
	return PROTO_INIT_OK;
}


