/**
 * CSF protocol API lib
 * Ver 4.0.0
 *
 */
 
#ifndef PROTOCOL_API
#define PROTOCOL_API

#include "mempool.h"
#include "ver_cntl.h"
#include "comm_proto.h"
#include "protocol.h"
#include "pipeline.h"
//#include "common.h"

#define CORE_VER_REQUIRE 0x00310000

void set_local_entry(LOCAL_ENTRY *);
void set_protocol_session_start(PROTOCOL_SESSION_START *);
void set_protocol_session_entry(PROTOCOL_SESSION_ENTRY *);
void set_protocol_session_end(PROTOCOL_SESSION_END *);

char *ip2str(CONN_INFO *cip, char *ipstr, size_t len);
int send_back(CONN_INFO *cip, const void *buf, const int len);


#endif


