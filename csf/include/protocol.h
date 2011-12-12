#ifndef _CSF_PROTOCOL_H
#define _CSF_PROTOCOL_H

#include <netinet/in.h>
#include "ver_cntl.h"
#include "csf.h"
#include "comm_proto.h"

#define PROTO_LIB_OK	0
#define PROTO_LIB_ERR	-1
#define PROTO_INIT_OK	0
#define PROTO_VER_ERR	-2
#define PROTO_CONF_ERR	-3
#define PROTO_INIT_ERR	-1

#define PROTOCOL_OK 0
#define PROTOCOL_ERROR -1
#define PROTOCOL_DISCONNECT -2

#define RESPONDER_OK 0
#define RESPONDER_CLEAN 0x1
#define RESPONDER_NOT_CLEAN 0x2
#define RESPONDER_DISCONNECT 0x4
#define RESPONDER_ERROR -1

typedef void *PROTOCOL_SESSION_START(CONN_INFO *);
typedef int PROTOCOL_SESSION_ENTRY(void *, CONN_INFO *, void *, void *, int);
typedef void PROTOCOL_SESSION_END(CONN_INFO*, void *);
typedef int LOCAL_ENTRY(int, char **);

typedef struct protocol_configs {
	PROTOCOL_SESSION_START *protocol_session_start;
	PROTOCOL_SESSION_ENTRY *protocol_session_entry;
	PROTOCOL_SESSION_END *protocol_session_end;
	LOCAL_ENTRY *local_entry;
} PROTO_CONFIG;

typedef struct protocol_parameter{	
	VCB *vcbp;	
	PROTO_CONFIG *pcp;	
	COMM_HANDLE *chp;
} PROTO_PARA;

typedef int _PROTOCOL_INIT(const char *, PROTO_PARA *);


void *do_protocol_session_start(CONN_INFO *);
int do_protocol_session_entry(void *, CONN_INFO *, void *, void *, int);
void do_protocol_session_end(CONN_INFO *, void *);

#endif

