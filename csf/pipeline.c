/*-
 * Copyright (C) 2007-2009 SINA Corporation, All Rights Reserved.
 *  Authors:
 *	  Zhu Yan <zhuyan@staff.sina.com.cn>
 * The POSIX Thread Pipeline Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <event.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <signal.h>
#include <sys/param.h>
#include <string.h>
#include <sys/time.h>
#include <dlfcn.h>

#include "queue.h"
#include "protocol.h"
#include "pipeline.h"
#include "pipeline_def.h"
#include "log.h"
//#include "common.h"
#include "utils.h"
#include "mod_conf.h"
#include "monitor.h"
#include "server.h"
#include "csf.h"

/* Thread status */
typedef enum thread_status {
	_THREAD_STARTING = 0,
	_THREAD_WAITING,
	_THREAD_BUSY,
	_THREAD_FAILED,
	_THREAD_DONE
}THREAD_STATUS ;

typedef struct pipeline_thread{
	/* XXX thread queue */
	TAILQ_ENTRY(pipeline_thread)	pl_thr_entries; 
	RQCB							*rqcb;
	pthread_t						tid;
	uint64_t						status; /* Current thread status */
	uint32_t						gen;	/* The thread generation */
	REQUEST							*cur_rqst;
	uint64_t						rqst_num;
}PIPELINE_THREAD;

uint64_t pipeline_queue_size = 0;	/* the limitation of queue size */
uint64_t pipeline_queue_len = 0;	 /* the counter of current queue length */
static int request_timeout = 0;	 /* request timeout, 0 is no limitted */
static uint64_t pipeline_working_threads = 0; /* the total number of working threads */
static uint64_t thread_current_gen = 0;
PCB *pcb[PIPELINE_SIZE];
struct active_pcb_queue apq;
static pthread_mutex_t	worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	worker_cond = PTHREAD_COND_INITIALIZER;
static uint32_t			initialized_thread_num = 0;
static int				pipeline_initialized = 0;
static pthread_attr_t	global_attr;
static pthread_t		main_thread;
static RQCB				done_rqcb;

static struct request_queue final_request_head =
	TAILQ_HEAD_INITIALIZER(final_request_head);

/* thread link list */
TAILQ_HEAD(pl_thr_queue, pipeline_thread) pl_thr_queue_head =
	TAILQ_HEAD_INITIALIZER(pl_thr_queue_head);


static int		done_signalled = 0;
static struct sched_param globsched;

extern int		done_fd;
extern int		done_fd_read;
extern long		clock_off;

extern int graceful_restart;
extern COMM_HANDLE *g_comm_handle;

extern void *stage_init(void *);
extern int stage_add(RQCB *);

/*
 * We have a temporary queue for request in the beginning, 
 * so that we wouldn't be blocked when it submit a request. 
 * When the target queue is busy, the request is put in the temporary queue, 
 * when we hold the lock,  move it to pipeline.

	
                           pipeline request queue
                           +--+--+              +--+--+--+  
                   +------>|  |  |    ......    |  |  |  |
                   |       +--+--+              +--+--+--+
                   | Yes
                   |
                 +--+
         request |  | Could we can hold the request_queue?
                 +--+
                   |  No
                   |       temporary request queue
                   |       +--+--+              +--+--+--+  
                   +------>|  |  |    ......    |  |  |  |  
                           +--+--+              +--+--+--+
*/
void kick_request_queue(void);
void broadcast_signal_to_allqueue(void);

void
kick_request_queue(void)
{
	PCB *pcbp;
	PCB *tmp_pcbp;
	
	/* kick "overflow" request queue */
	TAILQ_FOREACH_SAFE(pcbp, &apq, active_queue_entry, tmp_pcbp) {
		if (TAILQ_EMPTY(&(pcbp->tmp_request_head))) {
			/* Whatever we remove the no request PCB */
			TAILQ_REMOVE(&apq, pcbp, active_queue_entry);
			TAILQ_ENTRY_INIT(&(pcbp->active_queue_entry));
			continue;
		}

		if (pthread_mutex_trylock(&(pcbp->first_rqcbp->mutex)) == 0) {
			/* Normal path */
			TAILQ_CONCAT(&(pcbp->first_rqcbp->request_head), 
				&(pcbp->tmp_request_head), request_entry);
			pcbp->first_rqcbp->queue_len += pcbp->tmp_queue_len;
			pcbp->tmp_queue_len = 0;
			pthread_cond_signal(&(pcbp->first_rqcbp->cond));
			pthread_mutex_unlock(&(pcbp->first_rqcbp->mutex));
			TAILQ_REMOVE(&apq, pcbp, active_queue_entry);
			TAILQ_ENTRY_INIT(&(pcbp->active_queue_entry));
		}
	}
}

void
broadcast_signal_to_allqueue(void)
{
	int i;
	int rc;
	RQCB *cur_rqcbp;
	PCB *cur_pcbp;

	for (i = 0; i < PIPELINE_SIZE; i++) {
		cur_pcbp = pcb[i];
		if (cur_pcbp == NULL) {
			continue;
		} else {
			for (cur_rqcbp = cur_pcbp->first_rqcbp;
					cur_rqcbp != NULL; cur_rqcbp = cur_rqcbp->next_rqcb) { 
				rc = pthread_cond_broadcast(&(cur_rqcbp->cond));
				if (rc != 0)
					WLOG_ERR("pthread_cond_signal() errno: %d", errno);
			}
		}
	}
}


/*
 * submit_sub_request
 * Submit the request from current stage to the next stage
 * @rqcbp: The next request queue control block
 * @rqstp: The request
 */
static void 
to_next_stage(RQCB *rqcbp, REQUEST *rqstp)
{
	int rc;

	if (pthread_mutex_lock(&(rqcbp->mutex)) == 0) {
		if (rqcbp->queue_len >= pipeline_queue_size) {
			WLOG_ALERT("stage %s queue is full! queue_len=%u, queue_size=%u",
			rqcbp->mod_name, 
			(unsigned long)(rqcbp->queue_len), 
			(unsigned long)pipeline_queue_size);
		}

		/* Normal path */
		TAILQ_INSERT_TAIL(&(rqcbp->request_head), rqstp, request_entry);
		/* Internal housekeeping */
		rqcbp->queue_len++;
		rqcbp->request_count++;
		rqstp->nstage++;
		rc = pthread_cond_signal(&(rqcbp->cond));
		if (rc != 0) {
			WLOG_ERR("pthread_cond_signal() errno: %d", errno);
		}
		
		rc = pthread_mutex_unlock(&(rqcbp->mutex));	
		if(rc != 0) {
			WLOG_ERR("pthread_mutex_unlock() errno: %d", errno);
		}
	} else {
		WLOG_ERR("pthread_mutex_lock() errno: %d", errno);
	}
}

/*
 * request_cancel
 * Cancel a request
 * @req: The request
 */
static void
to_done_stage(REQUEST *rqstp) 
{
	int retval;

	if (pthread_mutex_lock(&(done_rqcb.mutex)) == 0) {
		
		TAILQ_INSERT_TAIL(&(done_rqcb.request_head), rqstp, request_entry);

		/* 
		 * this line of code is to check whether the insert excuted successful.
		 * if successful, the two tid printed should be same
		 */

		done_rqcb.rqst_num++;
		done_rqcb.queue_len++;
		retval = pthread_mutex_unlock(&(done_rqcb.mutex));
		if(retval != 0)
			WLOG_ERR("pthread_mutex_unlock() errno: %d", errno);
	} else {
		WLOG_ERR("pthread_mutex_lock() errno: %d", errno);
	}

	if (!done_signalled) {
		done_signalled = 1;
		
		/* 
		 * trigger the poll queue event 
		 * it means to inform the libevent to call funciton
		 * done_requests_handler() at a appropriate condition.
		 * libevent has added this event to its event set.
		 */
		 
		retval = write(done_fd, "zhuyan", 1);
		if(retval == -1)
			WLOG_ERR("trigger error: %d, fd: %d", errno, done_fd);
	}
}

/*
 * cleanup_request
 * Clean the "protocol data" when we submit the request
 * @head: The final queue
 * @rqstp: The request we want to clean
 */
static void 
cleanup_request(struct request_queue *head, REQUEST *rqstp) 
{
	CONN_STATE *csp;

	/* To remove the request from the request queue to clean */
	TAILQ_REMOVE(head, rqstp, request_entry);

	if (rqstp->data_cleaner != NULL){
		rqstp->data_cleaner(rqstp->proctocl_data);
	}
	
	csp = rqstp->csp;

	/* Free the csp if ref_count == 0 */
	if (csp != NULL) {
		CS_RELE(csp);
	}

	sfree(rqstp);
	return;
}

/*
 * get_done_requests
 * Get the request from the done queue 
 * @request_head: the final request queue
 */
static void
get_done_requests(struct request_queue *request_head)
{
	/* poll done queue */
	if (!TAILQ_EMPTY(&(done_rqcb.request_head))) {
		if (pthread_mutex_trylock(&done_rqcb.mutex) == 0) {
			TAILQ_CONCAT(request_head, &done_rqcb.request_head, 
				request_entry);
			done_rqcb.queue_len = 0;
			pthread_mutex_unlock(&done_rqcb.mutex);
		}
	}
}

char junk[16];
/*
 * done_requests_handler
 * To respond the pipeline after the request is complete
 */
/* ARGSUSED */
void
done_requests_handler(int fd, short event, void *arg)
{
	REQUEST	*rqstp;
	REQUEST	*tmpreq;
	int rc = RESPONDER_OK|RESPONDER_CLEAN;
	struct timeval	tv;
	struct event	*ev_pipe;
	CONN_STATE *csp;
	
	CSF_UNUSED_ARG(fd);
	
	ev_pipe = (struct event *) arg;

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	(void)event_add(ev_pipe, &tv);

	if (done_signalled) {
		(void)read(done_fd_read, junk, sizeof(junk));
		done_signalled = 0;
	}

	/* kick "overflow" request queue */
	kick_request_queue();

	/* Process the done request */
	if (!TAILQ_EMPTY(&(done_rqcb.request_head)) && event != EV_TIMEOUT) {
		get_done_requests(&final_request_head);
		TAILQ_FOREACH_SAFE(rqstp,
			&final_request_head, request_entry, tmpreq) {

			pipeline_queue_len--;

			csp = rqstp->csp;

			if (csp == NULL) {
				WLOG_EMERG("csp is NULL!");
				continue;
			}

			if (!rqstp->cancelled && rqstp->request_responder != NULL) {

				rc = rqstp->request_responder(csp, &(csp->ci), 
					rqstp->proctocl_data, rqstp->flags);

				if (rc == RESPONDER_ERROR) {
				/* XXX we should log something */
					WLOG_ERR("Failed to call request_responder!");
				}
			}
			
			if ((rc & RESPONDER_DISCONNECT) == RESPONDER_DISCONNECT) {

				if (csp->ci.cp_ops != NULL && 
					csp->ci.cp_ops->conn_close != NULL) {
					csp->ci.cp_ops->conn_close(&(csp->ci));
				}
				conn_state_cleanup(csp);
			}
			
			if ((rc & RESPONDER_CLEAN) != RESPONDER_CLEAN) {
				rqstp->data_cleaner = NULL;
			}

			cleanup_request(&final_request_head, rqstp);

			if (graceful_restart && pipeline_queue_len == 0) {
				broadcast_signal_to_allqueue();
			}
		}
	}
}


static inline void
clean_protocol_data(REQUEST *rqstp)
{
	if (rqstp != NULL && 
	rqstp->data_cleaner != NULL && 
	rqstp->proctocl_data != NULL) {

		rqstp->data_cleaner(rqstp->proctocl_data);
		rqstp->proctocl_data = NULL;
	}
	sfree(rqstp);
}

/*
 * init the worker thread
 */
static int
worker_thread_init(void *ptr, WTI *wtip, void **res)
{
	sigset_t new_set;
	int init_flag;
	PIPELINE_THREAD	*threadp = NULL;
	RQCB *rqcbp = NULL;
	char thread_name[64];

	threadp = (PIPELINE_THREAD*) ptr;
	rqcbp = threadp->rqcb;
	/* 
	 * Make sure to ignore signals which may possibly get sent to
	 * the parent thread. 
	 * Causes havoc with mutex's and condition waits otherwise.
	 */
	sigemptyset(&new_set);
	sigaddset(&new_set, SIGPIPE);
	sigaddset(&new_set, SIGCHLD);
	sigaddset(&new_set, SIGQUIT);
	sigaddset(&new_set, SIGTRAP);
	sigaddset(&new_set, SIGHUP);
	sigaddset(&new_set, SIGTERM);
	sigaddset(&new_set, SIGINT);
	sigaddset(&new_set, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &new_set, NULL);

	/* To notify the main thread this thread is initialized */
	pthread_mutex_lock(&worker_mutex);
	pipeline_working_threads++;

	/* set the working thread information */
	wtip->thr_seq_num = rqcbp->thread_num;
	wtip->next_rqcbp = NULL;
	
	snprintf(thread_name, 64, "%s:%03u", rqcbp->mod_name, wtip->thr_seq_num);
	//register_stat_int(rqcbp->mod_name, 
	//	thread_name, MNT_UINT_64 | MNT_GROUP_STAT, &(threadp->status));
	
	/* init the MOD */
	if (rqcbp->request_init != NULL) {
		/* XXX We should init every thread resource here */
		init_flag = rqcbp->request_init(/* IN */ wtip, /* OUT */ res);
		if (init_flag != PIPELINE_SUCCESS) {
			PRINT("Thread initialize failed!");
			pthread_mutex_unlock(&worker_mutex);
			CRIT("Thread initialize failed!");
		}
	} else {
		/* Make it */
		init_flag = PIPELINE_SUCCESS;
	}
	
	(rqcbp->thread_num)++;
	initialized_thread_num++;

	/* inform the init function that all threads have initialized */
	pthread_cond_signal(&worker_cond);
	pthread_mutex_unlock(&worker_mutex);
	
	return init_flag;
}

static void
submit_next(WTI *wtip, RQCB *rqcbp, REQUEST *rqstp)
{
	RQCB *next_rqcbp;

	switch (rqstp->ret) { 
	case PIPELINE_FAILED:
		WLOG_DEBUG("Request return error when go to stage %s[%d]: %p", 
			rqcbp->mod_name, rqcbp->queue_id, rqcbp->request_handler);
		/* Error, put the request in the done queue 
		 * Notice: We should check the return code 
		 * when we polling the done queue */
		
		to_done_stage(rqstp);
		break;
	case PIPELINE_CONTINUE:
		/* If the request is loop, we return error */
		if (rqstp->nstage > MAX_STAGE_JUMP) {
			rqstp->err = REQUEST_LOOP;
			WLOG_ERR("Request LOOP error!");
			to_done_stage(rqstp);
		}
		
		/* Add result to next_queue */
		if (wtip->next_rqcbp != NULL) {
			next_rqcbp = wtip->next_rqcbp;
			wtip->next_rqcbp = NULL;
		} else {
			next_rqcbp = rqcbp->next_rqcb;
		}
	
		if (next_rqcbp != NULL) {
			to_next_stage(next_rqcbp, rqstp);
		} else {
			/* 
			 * It's the last queue,
			 * we put the request to done queue.
			 */
			to_done_stage(rqstp);
		}
		break;
	case PIPELINE_BREAK:
		/*
		 * The worker thread told us to return.
		 * So we put the request to done queue 
		 */
		to_done_stage(rqstp);
		break;

	/* Re-insert the request to current queue */
	case PIPELINE_REQUEUE:
		/* If the request is loop, we return error */
		if (rqstp->nstage > MAX_STAGE_JUMP) {
			rqstp->err = REQUEST_LOOP;
			WLOG_ERR("Request LOOP error!");
			to_done_stage(rqstp);
		}
		
		/* Add result to next_queue */
		next_rqcbp = rqcbp;
	
		if (next_rqcbp != NULL) {
			to_next_stage(next_rqcbp, rqstp);
		} else {
			/* 
			 * It's the last queue,
			 * we put the request to done queue.
			 */
			to_done_stage(rqstp);
		}
		break;

	default:	
		/* XXX Just return ? */
		to_done_stage(rqstp);
	}
}

static int
thread_deinit(PIPELINE_THREAD	*threadp)
{	
	pthread_mutex_lock(&worker_mutex);
	TAILQ_REMOVE(&pl_thr_queue_head, threadp, pl_thr_entries);
	free(threadp);
	pipeline_working_threads--;
	
	/*
	 * XXX is it precise? 
	 * XXX is sure the last request SHOULD not be submitted?
	 */
	if (pipeline_working_threads == 0) {
		pthread_mutex_unlock(&worker_mutex);
		exit(0);
	}
	pthread_mutex_unlock(&worker_mutex);

	return (CSF_OK);
}

/*
 * worker_thread
 * @ptr: pl_thread_t structure
 * The primary function in pipeline, to dispatch the thread.
 * This function is loaded by pthread_create, and it should never return.
 */
static void* 
worker_thread(void *ptr)
{
	PIPELINE_THREAD	*threadp = NULL;
	RQCB *rqcbp = NULL;
	REQUEST	*rqstp = NULL;
	void *res = NULL;
	int init_flag;
	WTI wti;
	long cl;

	if (ptr == NULL) {
		return (NULL);
	}
		
	threadp = (PIPELINE_THREAD*) ptr;
	rqcbp = threadp->rqcb;

	init_flag = worker_thread_init(ptr, &wti, &res);

	for(;;) {
		rqstp = NULL;
		threadp->cur_rqst = NULL;

		/* Get a request to process */
		threadp->status = _THREAD_WAITING;

		pthread_mutex_lock(&(rqcbp->mutex));
		/* Waiting for a node from the queue */
		while (TAILQ_EMPTY(&rqcbp->request_head)) {
			pthread_cond_wait(&(rqcbp->cond), &rqcbp->mutex);
			/* 
			 * When the generation of thread is not current generation,
			 * we cancel the thread. 
			 * It means we have re-configured the thread number, 
			 * and canceled current thread
			 */
			if (graceful_restart && pipeline_queue_len == 0) {
				if (rqcbp->request_deinit != NULL)
					rqcbp->request_deinit(&wti, res);
				pthread_mutex_unlock(&(rqcbp->mutex));
				return (thread_deinit(threadp));
			}
		}

		/* get the node off */
		rqstp = TAILQ_FIRST(&(rqcbp->request_head)); 
		if (rqstp == NULL) {
			continue;
		}

		cl = clock_off - rqstp->clock;

		/* if the request is not in time, don't get it off,  
		 * just delay the thread and continue for the next request
		 */
		if (rqcbp->delay) {
			if (cl < rqcbp->delay) {
				pthread_mutex_unlock(&(rqcbp->mutex));
				usleep((rqcbp->delay - cl) * 1000000);
				continue;
			}
		}

		TAILQ_REMOVE(&(rqcbp->request_head), rqstp, request_entry);
		if (rqcbp->queue_len == 0) {
			WLOG_ERR("rqcbp->queue_len is 0!");
		}

		rqcbp->queue_len--;
		pthread_mutex_unlock(&(rqcbp->mutex));

		/* If timed out, we send it to done queue */
		if (request_timeout != 0 && 
			rqstp->clock != 0 &&
			cl > request_timeout) {
			rqstp->flags = REQUEST_TIME_OUT;
			to_done_stage(rqstp);
			continue;
		}

		/* process the request */
		threadp->status = _THREAD_BUSY;
		threadp->cur_rqst = rqstp;
		errno = 0;

		/* XXX Should we do something when the request is cancelled? */
		if (!rqstp->cancelled) {
			if (rqcbp->request_handler && init_flag == PIPELINE_SUCCESS) {
				rqstp->ret = rqcbp->request_handler(&wti, &(rqstp->csp->ci),
					rqstp->proctocl_data, &res);
				rqstp->err = errno;
				submit_next(&wti, rqcbp, rqstp);
			} else {
				/* XXX Should we pass the request to next_queue? */
				to_done_stage(rqstp);
			}
		} else {
			/* Cancelled */
			rqstp->ret = 0;
			rqstp->err = 0;
			WLOG_DEBUG("Thread %p is cancelled!", &(threadp->tid));
			to_done_stage(rqstp);
		}
		//rqcbp->queue_len--;
		threadp->rqst_num++;
		threadp->status = _THREAD_DONE;

	} /* loop forever */
	/* NOTREACHED */

	WLOG_ERR("FATAL ERROR! Thread exit!");
	threadp = NULL;

	if (rqcbp->request_deinit != NULL) {
		rqcbp->request_deinit(&wti, res);
	}

	return (NULL);
}   /* worker_thread */


static int 
request_queue_init
(RQCB *rqcbp, 
 RQCB *next_rqcbp,
 uint32_t delay,
 REQUEST_INIT *rip,
 REQUEST_DEINIT *rdp,
 REQUEST_HANDLER *rhp
)
{
	rqcbp->rqst_num = 0;
	rqcbp->queue_len = 0;
	rqcbp->thread_num = 0;
	rqcbp->next_rqcb = next_rqcbp;
	rqcbp->request_init = rip;
	rqcbp->request_deinit = rdp;
	rqcbp->request_handler = rhp;
	rqcbp->request_count = 0;
	rqcbp->delay = delay;
	TAILQ_INIT(&(rqcbp->request_head));

	/* Initialize request queue */
	if (pthread_mutex_init(&(rqcbp->mutex), NULL)) {
		WLOG_ERR("Failed to create mutex");
		return (CSF_ERR);
	}
	if (pthread_cond_init(&(rqcbp->cond), NULL)) {
		WLOG_ERR("Failed to create condition variable");
		return (CSF_ERR);
	}

	return (CSF_OK);
}

/*
 * thread_pool_init
 * @request_queue: the request_queue be initialized
 * @next_queue: the next queue after request_queue
 * @nthread: thread number
 *
 * Initialize n threads for a queue
 */
static int 
thread_pool_init(RQCB *rqcbp, int nthread)
{
	PIPELINE_THREAD	*threadp = NULL;
	int i;
	int rc;

	WLOG_INFO("Start to initiate thread group includes %d threads", nthread);
	for (i = 0; i < nthread; i++) {
		threadp = (PIPELINE_THREAD *)calloc(1, sizeof(PIPELINE_THREAD));
		if (threadp == NULL) {
			return (CSF_ERR);
		}
		threadp->status = _THREAD_STARTING;
		threadp->cur_rqst = NULL;
		threadp->rqst_num = 0; /* XXX not used? */
		threadp->gen = thread_current_gen;
		threadp->rqcb = rqcbp;

		rc = pthread_create(&threadp->tid, 
			&global_attr, worker_thread, threadp);
		if (rc != 0) {
			WLOG_ERR("Create thread failed!");
			threadp->status = _THREAD_FAILED;
			sfree(threadp); 
			threadp = NULL;
			continue;
		}
		
		rc = pthread_detach(threadp->tid);
		if (rc != 0)
			WLOG_ERR("Detach thread failed!");

		TAILQ_INSERT_TAIL(&pl_thr_queue_head, threadp, pl_thr_entries);
	}
	return (CSF_OK);
}

static void
free_pipeline_data(PCB **pcbpp)
{
	int i;
	RQCB *cur_rqcbp;

	for (i = 0; i < PIPELINE_SIZE; i++) {
		if (pcbpp[i] != NULL) {
			for (cur_rqcbp = pcbpp[i]->first_rqcbp;
					cur_rqcbp != NULL; cur_rqcbp = cur_rqcbp->next_rqcb) { 
				free(cur_rqcbp);
			}
			free(pcbpp[i]);
		}
	}
}

void *
request_info_init(CSF_CONF *conf)
{
	REQUEST_INFO *rip;
	
	/* init the submit_request info block */
	rip = (REQUEST_INFO *)calloc(1, sizeof(REQUEST_INFO));
	if (rip == NULL) {
		WLOG_ERR("not enough memory.");
		return NULL;
	}
	
	/* fill the submit_request info block */
	rip->pipeline_queue_size = conf->pipeline_queue_size;
	rip->pipeline_queue_len = &pipeline_queue_len;
	rip->pcb = pcb;
	rip->apqp = &apq;
	rip->graceful_restart = &graceful_restart;
	rip->clockp = &clock_off;
	rip->done_signal = &done_signalled;
	rip->final_request_queue = &final_request_head;
	rip->done_fd = done_fd;
	rip->request_timeout = conf->request_timeout;

	return rip;
}


/*
 * initialize pipeline 
 */
int 
pipeline_init(void *qp, int queue_size, int timeout)
{
	RQCB *rqcbp;
	RQCB *next_rqcbp;
	RQCB *next_arg;
	MOD_CONFIG *mcp = NULL;
	MOD_CONFIG *tmcp = NULL;
	MOD_CONFIG *next_mcp = NULL;
	PCB *pcbp;
	uint32_t all_thread_num = 0;
	int i;
	int rc;
	int qid = 0;
	uint32_t cur_id;
	TAILQ_HEAD(, mod_config) *mqp;

	mqp = qp;
	pipeline_initialized = 0;
	request_timeout = timeout;

	for (i = 0; i < PIPELINE_SIZE; i++) {
		pcb[i] = NULL;
	}
	TAILQ_INIT(&apq);

	if (TAILQ_EMPTY(mqp)) {
		PRINT("No MOD information in configure file.");
		return (PIPELINE_CONF_ERR);
	}

	WLOG_INFO("Starting to initialize thread pool...");

	if (queue_size >= 0 && queue_size < MAX_PIPELINE_QUEUE_SIZE) {
		pipeline_queue_size = queue_size;
	} else {
		PRINT("Queue size is not a valid value.");
		return (PIPELINE_CONF_ERR);
	}

	//register_stat_int("main", "all_queue_len", MNT_UINT_64, &pipeline_queue_len);
//	monitor_var_register("main", "all_queue_len", &pipeline_queue_len, MNT_TYPE_UINT64);

	/* initialize global_attr with all the default thread attributes */
	pthread_attr_init(&global_attr);
	pthread_attr_setscope(&global_attr, PTHREAD_SCOPE_SYSTEM);
	globsched.sched_priority = 1;
	main_thread = pthread_self();
	pthread_setschedparam(main_thread, SCHED_OTHER, &globsched);
	globsched.sched_priority = 2;
	pthread_attr_setschedparam(&global_attr, &globsched);

	/* Give each thread a smaller 256KB stack, should be more than sufficient */
	pthread_attr_setstacksize(&global_attr, THREAD_STACK_SIZE);

	TAILQ_INIT(&pl_thr_queue_head);
	g_comm_handle->stagep = stage_init(NULL);

	if (pthread_mutex_init(&worker_mutex, NULL)) {
		WLOG_ERR("Failed to create mutex");
		return (PIPELINE_INIT_ERR);
	}
	
	if (pthread_cond_init(&worker_cond, NULL)) {
		WLOG_ERR("Failed to create condition variable");
		return (PIPELINE_INIT_ERR);
	}
	
	/* Initialize the first queue */
	rqcbp = (RQCB *)calloc(1, sizeof(RQCB));
	
	if (rqcbp == NULL)
		return (PIPELINE_INIT_ERR);

	TAILQ_FOREACH_SAFE(mcp, mqp, mod_entries, tmcp) {
		strlcpy(rqcbp->mod_name, mcp->mod_name, MAX_MOD_NAME_LEN);
		cur_id = mcp->pipeline_id;

		rc = stage_add(rqcbp);

		if (rc == STAGE_ADD_ERR) {
			return (PIPELINE_INIT_ERR);
		}

		if (pcb[cur_id] == NULL) {
			pcbp = (PCB *)calloc(1, sizeof(PCB));
			if (pcbp== NULL) {
				free_pipeline_data(pcb);
				return (PIPELINE_INIT_ERR);
			}

			TAILQ_ENTRY_INIT(&(pcbp->active_queue_entry));

			TAILQ_INIT(&(pcbp->tmp_request_head));
			pcbp->first_rqcbp = rqcbp;
			pcbp->pipeline_id = cur_id;
			pcb[cur_id] = pcbp;
		}

		next_mcp = TAILQ_NEXT(mcp, mod_entries);

		if (next_mcp == NULL) {
			next_rqcbp = NULL;
			next_arg = NULL;
		} else {
			next_rqcbp = (RQCB *)calloc(1, sizeof(RQCB));
			if (next_rqcbp == NULL) {
				free_pipeline_data(pcb);
				return (PIPELINE_INIT_ERR);
			}

			if (next_mcp->pipeline_id == cur_id) {
				next_arg = next_rqcbp;
				rqcbp->queue_id = qid;
				qid++;
			} else {
				next_arg = NULL;
				qid = 0;
			}
		}

		/* monitor the queue len of each working RQCB */
//		monitor_var_register(rqcbp->mod_name, "queue_len", &(rqcbp->queue_len), MNT_TYPE_UINT32);

		/* monitor the reqeust cumulation of each RQCB */
//		monitor_var_register(rqcbp->mod_name, "request_counter", &(rqcbp->request_counter), MNT_TYPE_UINT64);

		rc = request_queue_init(rqcbp, next_arg, mcp->delay, 
			mcp->request_init, mcp->request_deinit, mcp->request_handler);

		if (rc != CSF_OK)
			CRIT("request_queue_init() error!");

		all_thread_num += mcp->thread_num;
		
		thread_pool_init(rqcbp, mcp->thread_num);
		rqcbp = next_rqcbp;
		TAILQ_REMOVE(mqp, mcp, mod_entries);
		free(mcp);
	}

	/* To wait for all pipeline threads is initialized */
	pthread_mutex_lock(&worker_mutex);
	while (all_thread_num != initialized_thread_num) {
		pthread_cond_wait(&worker_cond, &worker_mutex);
	}
	pthread_mutex_unlock(&worker_mutex);

	WLOG_INFO("All thread(s) is initialized!");

	if (request_queue_init(&done_rqcb, NULL, 0, NULL, NULL, NULL) != CSF_OK) {
		CRIT("request_queue_init() error!");
	}

	pipeline_initialized = 1;
	return (PIPELINE_INIT_OK);
}

