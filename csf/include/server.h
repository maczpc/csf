#ifndef _CSF_SERVER_H
#define _CSF_SERVER_H

#include "csf.h"

#define RECV_DATA_LEN	(80*1024 - 1)

/* LMTP STATE */
enum COMMUNICATION_PROTOCOL {
	CSF_TCP = 1,
	CSF_UDP,
	CSF_SCTP,
	CSF_UN
};

typedef struct conn_state{
	struct event	ev;	/* libevent event struct */
	CONN_INFO ci;		/* connection information for communication */
	size_t	ref_cnt;	/* reference count for release */
	void		*data;	/* protocol data */
} CONN_STATE;


#define	CS_HOLD(csp)	do { \
	(csp)->ref_cnt++; \
} while (0)

#define	CS_RELE(csp)	do { \
	csp->ref_cnt--; \
	if (csp->ref_cnt == 0) \
		sfree(csp); \
} while (0)


void server_init(CSF_CONF *);
void conn_state_cleanup(CONN_STATE *);

#endif

