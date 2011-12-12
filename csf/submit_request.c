/*-
 * Copyright (C) 2007-2009 SINA Corporation, All Rights Reserved.
 *  Authors:
 *	  Zhu Yan <zhuyan@staff.sina.com.cn>
 * The POSIX Thread Pipeline Interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#include "tree.h"
//#include "common.h"
#include "utils.h"
#include "pipeline.h"
#include "pipeline_def.h"
#include "log.h"
#include "server.h"
#include "mod_conf.h"

REQUEST_INFO *req_info = NULL;

void
submit_request_init(void *handle)
{
	req_info = (REQUEST_INFO *)handle;
}

static int
send_to_pipeline(
PCB *pcbp,
REQUEST *rqstp,
uint32_t flags,
int concat)
{
	RQCB *first_rqcbp = NULL;
	REQUEST *out_rqstp = NULL;
	REQUEST *timeout_rqstp = NULL;
	REQUEST *t_rqstp = NULL;
	int	rc;
	int notice_flags = 0;

	first_rqcbp = pcbp->first_rqcbp;

	if (pthread_mutex_trylock(&(first_rqcbp->mutex)) == 0) {
		if (concat) {
			/* Move the buffered request to the pipeline */
			TAILQ_CONCAT(&(first_rqcbp->request_head), 
				&(pcbp->tmp_request_head),
				request_entry);
			
			first_rqcbp->queue_len += pcbp->tmp_queue_len;
			pcbp->tmp_queue_len = 0;

			if (!TAILQ_ENTRY_EMPTY(&(pcbp->active_queue_entry))) {
				TAILQ_REMOVE(req_info->apqp, pcbp, active_queue_entry);
				TAILQ_ENTRY_INIT(&(pcbp->active_queue_entry));
			}
		}

		/* Normal path */
		if (!(flags & REQUEST_FORCE) &&
			(first_rqcbp->queue_len >= req_info->pipeline_queue_size)) {
			WLOG_ALERT("pipeline %d queue is full! queue_len=%u, flags=%u", 
				pcbp->pipeline_id,
				(unsigned long)(first_rqcbp->queue_len), flags);
			pthread_mutex_unlock(&(first_rqcbp->mutex));	
			return (SUBMIT_QUEUE_FULL);
		}

		/*
		 * If we set REQUEST_KICK_OUT flag, 
		 * we will kick out a old request in the waiting queue.
		 * Remove it from the waiting queue, and insert it to the final queue.
		 */
		if ((flags & REQUEST_FORCE) &&
			(flags & REQUEST_KICK_OUT) &&
			(first_rqcbp->queue_len >= req_info->pipeline_queue_size)) {
			out_rqstp = TAILQ_FIRST(&(first_rqcbp->request_head));
			out_rqstp->flags = REQUEST_KICK_OUT;
			TAILQ_REMOVE(&(first_rqcbp->request_head),
				out_rqstp, request_entry);
			(*(req_info->pipeline_queue_len))--;
			first_rqcbp->queue_len--;
			TAILQ_INSERT_TAIL(req_info->final_request_queue, out_rqstp, 
				request_entry);
			notice_flags = 1;
		}

		/*
		 * If we set REQUEST_TIME_OUT flag, 
		 * we will kick out requests in the waiting queue when it timed out.
		 * Remove it from the waiting queue, and insert it to the final queue.
		 */

		if ((flags & REQUEST_TIME_OUT) && (req_info->request_timeout != 0)) {
			TAILQ_FOREACH_SAFE(timeout_rqstp, &(first_rqcbp->request_head),
				request_entry, t_rqstp) {
				if ((*(req_info->clockp) - timeout_rqstp->clock) >
					req_info->request_timeout) {
					TAILQ_REMOVE(&(first_rqcbp->request_head),
						timeout_rqstp, request_entry);
					(*(req_info->pipeline_queue_len))--;
					first_rqcbp->queue_len--;
					TAILQ_INSERT_TAIL(req_info->final_request_queue,
						timeout_rqstp, request_entry);
					notice_flags = 1;
				}
			}
		}
	
		TAILQ_INSERT_TAIL(&(first_rqcbp->request_head), 
			rqstp, request_entry);

		(*(req_info->pipeline_queue_len))++;
	
		/* Internal housekeeping */
		first_rqcbp->queue_len++;
		
		rc = pthread_cond_signal(&(first_rqcbp->cond));
		if (rc != 0) {
			WLOG_ERR("pthread_cond_signal() error: %s", strerror(errno));
			pthread_mutex_unlock(&(first_rqcbp->mutex));	
			return (SUBMIT_GENERIC_ERROR);
		}
	
		rc = pthread_mutex_unlock(&(first_rqcbp->mutex));			
		if (rc != 0) {
			WLOG_ERR("pthread_mutex_unlock() error: %s", strerror(errno));
			return (SUBMIT_GENERIC_ERROR);
		}

		/* If a request is kicked, we notice csf to get it. */
		if (notice_flags && !(*(req_info->done_signal))) {
			*(req_info->done_signal) = 1;
			rc = write(req_info->done_fd, "zhuyan", 1);
			if (rc == -1) {
				WLOG_ERR("trigger error: %d, fd: %d", errno, req_info->done_fd);
			}
		}

	} else {
		/* Oops, the request queue is blocked, use tmp_request_queue */
		TAILQ_INSERT_TAIL(&(pcbp->tmp_request_head), rqstp, request_entry);
		pcbp->tmp_queue_len++;
		(*(req_info->pipeline_queue_len))++;
		if (TAILQ_ENTRY_EMPTY(&(pcbp->active_queue_entry))) {
			TAILQ_INSERT_TAIL(req_info->apqp, pcbp, active_queue_entry);
		}
	}
	return (0);
}

/*
 * submit_request
 * Submit the request to the pipeline.
 * This is the entry for pipeline
 * @pipeline_id: pipeline id from 0-15
 * @req: The request from connection state
 * @data: The data for submit
 * @data_cleaner: Clean the data buffer
 * @responder: After the request returned, call it back.
 * @force: if force is false, when the request queue is full, 
 * submit_request() return -2, else submit_request() 
 * will submit the request recklessly 
 */
int64_t 
submit_request(
void *p,
uint32_t pipeline_id,
void *data,
DATA_CLEANER *data_cleaner, 
REQUEST_RESPONDER *responder,
uint32_t flags)
{
	REQUEST	*rqstp;
	CONN_STATE *csp;
	int	rc;
	PCB *pcbp;
	uint64_t tid;
	RQCB *first_rqcbp;
	static uint64_t inc = 0;

	if (pipeline_id >= PIPELINE_SIZE) {
		return (SUBMIT_GENERIC_ERROR);
	}

	/*
	 * XXX might be not forbid the last request submitted
	 */
	if (*(req_info->graceful_restart)) {
		return (SUBMIT_GENERIC_ERROR);
	}

	pcbp = req_info->pcb[pipeline_id];
	if (pcbp == NULL) {
		return (SUBMIT_GENERIC_ERROR);
	}

	first_rqcbp = pcbp->first_rqcbp;
	if (first_rqcbp == NULL) {
		return (SUBMIT_GENERIC_ERROR);
	}

	/* 
	 * It could lead each pipeline worker mod can not check the pipeline queue size.
	 * Like this: (pcbp->first_rqcbp)->queue_len >= pipeline_queue_size)
	 * It would be invalid.
	 */
	if ((!(flags & REQUEST_FORCE)) &&
		(*(req_info->pipeline_queue_len) >= req_info->pipeline_queue_size)) {
		WLOG_ALERT("pipeline %d queue is full! " \
			"queue_len=%ull queue_size=%ull flags=%u",
			pipeline_id, *(req_info->pipeline_queue_len),
			req_info->pipeline_queue_size, flags);
		return (SUBMIT_QUEUE_FULL);
	}

	/* Mark it as not executed (failing result, no error) */
	rqstp = (REQUEST *)smalloc(sizeof(REQUEST));
	rqstp->cancelled = 0;
	rqstp->ret = -1;
	rqstp->err = 0;
	rqstp->flags = REQUEST_NONE;
	rqstp->nstage = 0;
	rqstp->clock = *(req_info->clockp);
	rqstp->proctocl_data = data;
	rqstp->data_cleaner = data_cleaner;
	rqstp->request_responder = responder;

	if (p != NULL) {
		csp = (CONN_STATE*)p;
		CS_HOLD(csp);
		rqstp->csp = csp;
	}

	/* Generate a transaction id */
	pipeline_id &= 0x0000007f;
	tid = pipeline_id;
	tid <<= 56;
	inc++;
	tid += inc;
	rqstp->tid = tid;

	/* Play some tricks with the tmp_request_queue queue */
	if (TAILQ_EMPTY(&(pcbp->tmp_request_head))) {
		rc = send_to_pipeline(pcbp, rqstp, flags, 0);
		if (rc != 0) {
			return (SUBMIT_GENERIC_ERROR);
		}
	} else {
		/* Secondary path. We have blocked requests to deal with */
		/* add the request to the chain */
		rc = send_to_pipeline(pcbp, rqstp, flags, 1);
		if (rc != 0) {
			return (SUBMIT_GENERIC_ERROR);
		}
	}
	
	(pcbp->counter)++;	/* counter of each PCB */
	(first_rqcbp->request_count)++;	 /* counter of each RQCB */

	return ((int64_t)tid);
	/* Warn if out of threads */
	/* XXX We should do more here */
}

/*
 * request_cancel
 * Cancel a request
 * @req: The request
 */
void
request_cancel(void *req)
{
	REQUEST *reqp;

	if (req == NULL) {
		return;
	}
	reqp = (REQUEST *)req;
	if (reqp != NULL) {
		reqp->cancelled = 1;
	}
}

