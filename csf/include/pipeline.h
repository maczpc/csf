#ifndef _CSF_PIPELINE_H
#define _CSF_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "protocol.h"
#include "queue.h"
#include "tree.h"

#include <sys/param.h>
#include <pthread.h>
#include <stdint.h>

#define MAX_MOD_NAME_LEN 64

#define PIPELINE_SUCCESS	0
#define PIPELINE_FAILED		-1
#define PIPELINE_CONTINUE	0
#define PIPELINE_BREAK		1
#define PIPELINE_REQUEUE	2

#define DEFAULT_THREAD_NUMBER	8
#define DEFAULT_THREAD_STAGE_ID	1
#define DEFAULT_PIPELINE_ID		0

#define PIPELINE_SIZE	16
#define MAX_STAGE_SIZE	32
#define MAX_STAGE_JUMP	256

#define MAX_PIPELINE_QUEUE_SIZE	0x7FFFFFFFL

#define SUBMIT_GENERIC_ERROR	-1
#define SUBMIT_QUEUE_FULL		-2

#define REQUEST_LOOP	-3


#define RESPONDER_OK 0
#define RESPONDER_CLEAN 0x1
#define RESPONDER_NOT_CLEAN 0x2
#define RESPONDER_DISCONNECT 0x4

#define RESPONDER_ERROR -1

#define STAGE_ADD_ERR	-1
#define STAGE_ADD_OK	0
#define STAGE_SET_ERR	-1
#define STAGE_SET_OK	0

#define REQUEST_NONE 0
#define REQUEST_FORCE (1<<0)
#define REQUEST_KICK_OUT (1<<1)
#define REQUEST_TIME_OUT (1<<2)

typedef struct worker_thread_info WTI;

typedef ssize_t REQUEST_INIT(void *, void **);
typedef void    REQUEST_DEINIT(void *, void *);
typedef ssize_t REQUEST_HANDLER(WTI *, CONN_INFO *, void *, void **);

typedef void DATA_CLEANER(void *);
typedef ssize_t REQUEST_RESPONDER(void *, CONN_INFO *, void *, int);


struct worker_thread_info{
	uint32_t	thr_seq_num; 
	void *next_rqcbp;
};


void	done_requests_handler(int, short, void *);
void	request_cancel(void *);
void 	*request_info_init(CSF_CONF *);
void	submit_request_init(void *);
int64_t	submit_request(void *, uint32_t, void *, DATA_CLEANER *, 
					REQUEST_RESPONDER *, uint32_t);

//extern int set_next_stage(WTI *, char *);

#ifdef __cplusplus
}
#endif

#endif

