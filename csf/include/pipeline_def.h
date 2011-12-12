#ifndef _CSF_PIPELINE_DEF_H
#define _CSF_PIPELINE_DEF_H

#include "tree.h"
#include "server.h"
#include "mod_conf.h"

#define THREAD_STACK_SIZE (1024*256)
#define DEFAULT_THREAD_GEN 1

#define PIPELINE_INIT_OK		0
#define PIPELINE_INIT_ERR		-1
#define PIPELINE_CONF_ERR		-2

/* The request node */
typedef struct request {
	TAILQ_ENTRY(request)	request_entry;
	/* When the whole request data flow run over, call it */
	REQUEST_RESPONDER		*request_responder;
	void					*proctocl_data;	/* lmtp_data_t */
	DATA_CLEANER			*data_cleaner;	/* To clean proctocl_data */
	uint32_t				pipeline_id;
	uint64_t				tid;			/* Transation ID */
	uint32_t				nstage;			/* stage counter*/
	long					clock;			/* clock of request for timeout */
	CONN_STATE				*csp;
	int						flags;
	int						ret;
	int						err;
	uint32_t				cancelled;		/* cancelled is set by main thread */
} REQUEST;


TAILQ_HEAD(request_queue, request);

/* Request Queue Control Block */
typedef struct rqcb {
	pthread_mutex_t			mutex;
	pthread_cond_t			cond;
	struct request_queue	request_head;	 /* The request queue head */  
	RB_ENTRY(rqcb)			stage_entry;
	struct rqcb				*next_rqcb;   /* next request queue */
	char					mod_name[MAX_MOD_NAME_LEN];
	int						queue_id;
	/* if the pipeline need to return when it done, set it to true*/
	uint32_t				queue_len; /* current request queue length */
	uint32_t				thread_num; /* current request queue length */
	uint32_t				delay;
	REQUEST_INIT			*request_init;	 /* Each request initializer */
	REQUEST_DEINIT			*request_deinit;	 /* Each request initializer */
	REQUEST_HANDLER			*request_handler;  /* Each request handler */
	uint32_t				rqst_num;		  /* request number */
	uint64_t				request_count;	  /* how many submitted requests */
} RQCB;

/* Pipeline Control Block */
typedef struct pcb {
	uint64_t	counter;
	uint32_t	pipeline_id;
	RQCB		*first_rqcbp;
	TAILQ_ENTRY(pcb) active_queue_entry;
	/*
	 * The temporary list head for request_head.
	 * When we can not add the node to request_head, we should add it to here 
	 * MAKE SURE to process it in a single thread, so we needn't lock it.
	 */
	uint32_t	tmp_queue_len;
	struct request_queue	tmp_request_head;
} PCB;

TAILQ_HEAD(active_pcb_queue, pcb);

typedef struct request_info {
	uint64_t	pipeline_queue_size;
	uint64_t	*pipeline_queue_len;
	int			*graceful_restart;
	PCB			**pcb;
	struct active_pcb_queue *apqp;
	long		*clockp;
	int			*done_signal;
	struct request_queue	*final_request_queue;
	int			done_fd;
	int			request_timeout;
} REQUEST_INFO;

int	pipeline_init(void *, int, int);

#endif
