#include <errno.h>
#include <arpa/inet.h>

#include "protocol.h"
#include "confparser.h"
#include "log.h"
#include "server.h"
#include "pipeline.h"
//#include "common.h"
#include "utils.h"
#include "monitor.h"

static _PROTOCOL_INIT *_protocol_init;

/* XXX module.c externed this funciton. it should not be Static */
void set_protocol_init(_PROTOCOL_INIT *);
int app_proto_init(const char *, COMM_PROTO_OPS *, VCB *, COMM_HANDLE *);
char *ip2str(CONN_INFO *, char *, size_t);

PROTO_CONFIG pc = {
	.protocol_session_start = NULL,
	.protocol_session_entry = NULL,
	.protocol_session_end = NULL,
	.local_entry = NULL,
};

void
set_protocol_init(_PROTOCOL_INIT *init)
{
	_protocol_init = init;
}

void *
do_protocol_session_start(CONN_INFO *cip)
{
	if (pc.protocol_session_start != NULL && cip != NULL) {
		return (pc.protocol_session_start(cip));
	} else {
		WLOG_ERR("protocol_session_start() is NULL!");
		return (NULL);
	}
}

int 
do_protocol_session_entry(void *csp, 
	CONN_INFO *cip, void *prot_data, void *data, int len)
{	
	if (pc.protocol_session_entry != NULL) {
		return (pc.protocol_session_entry(csp, cip, prot_data, data, len));
	} else {
		WLOG_ERR("protocol_session_entry() is NULL!");
		return (CSF_OK);
	}
}

void 
do_protocol_session_end(CONN_INFO *cip, void *prot_data)
{
	if (pc.protocol_session_end != NULL) {
		pc.protocol_session_end(cip, prot_data);
	} else {
		WLOG_ERR("protocol_session_end() is NULL!");
	}
}

char *
ip2str(CONN_INFO *cip, char *ipstr, size_t len)
{
	if (inet_ntop(AF_INET, &(cip->addr.sin_addr), ipstr, len) == NULL) {
		return (NULL);
	}
	return ipstr;
}


int 
app_proto_init(const char *prot_name, COMM_PROTO_OPS *ops, VCB *vcbp, 
			   COMM_HANDLE *comm_handle)
{
    int rv;
	PROTO_PARA ppp;

	CSF_UNUSED_ARG(ops);
	
    if (_protocol_init != NULL) {
		ppp.chp = comm_handle;
		ppp.pcp = &pc;
		ppp.vcbp = vcbp;
		
		rv = _protocol_init(prot_name, &ppp);
        if (rv < 0) {
			switch (rv) {
				case PROTO_INIT_ERR :
					PRINT("PROTOCOL init failed. PROTO_INIT_ERR returned.");
					WLOG_ERR("PROTOCOL init failed. PROTO_INIT_ERR returned.");
					break;
				case PROTO_VER_ERR :
					PRINT("PROTOCOL version mismatch. You need CSF higher version.");
					WLOG_ERR("PROTOCOL version mismatch. You need CSF higher version.");
					break;
				default :
					PRINT("PROTOCOL unknown error. return value less than zero.");
					WLOG_ERR("PROTOCOL unknown error. return value less than zero.");
					break;
			}
			return (CSF_ERR);
        }
	} else {
        WLOG_ERR("No protocol module found.");
        return (CSF_ERR);
    }      

    return (CSF_OK);
}

