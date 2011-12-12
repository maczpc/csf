/**
 * CSF module API lib
 * Ver 4.0.0
 *
 */
 
#ifndef MODULE_API
#define MODULE_API

#include <sys/uio.h>
#include "comm_proto.h"
#include "mod_conf.h"
#include "ver_cntl.h"
//#include "common.h"

#define CORE_VER_REQUIRE 0x00400000

int send_file(CONN_INFO *, int, size_t, off_t *, off_t *);
ssize_t writev_back(CONN_INFO *, const struct iovec *, uint16_t, int);
ssize_t write_back(CONN_INFO *, const char *, size_t, int);

void set_request_init(REQUEST_INIT *);
void set_request_handler(REQUEST_HANDLER *);
void set_request_deinit(REQUEST_DEINIT *);

extern void *stage_init(void *);
extern int set_next_stage(WTI *, char *);


#endif

