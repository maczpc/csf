/*-
 * Copyright (c) 2007-2009 SINA Corporation, All Rights Reserved.
 *  Authors:
 *      Zhu Yan <zhuyan@staff.sina.com.cn>
 * Data Transfer Layer
 */

#include "utils.h"
#include "data.h"
#include "protocol.h"
#include "log.h"

extern char *ip2str(CONN_INFO *, char *, size_t);

int connection_counter = 0;
static char ip_str[32];

int 
connection_made(CONN_STATE *csp)
{
	CONN_INFO *cip;

	if (csp == NULL) {
		WLOG_DEBUG("csp is NULL!");
		return (CSF_ERR);
	}
	cip = &(csp->ci);

	ip2str(cip, ip_str, 16);
	WLOG_INFO("Connection made, fd: %d, IP: %s", cip->fd, ip_str);
	connection_counter++;
	csp->data = do_protocol_session_start(cip);
	return (CSF_OK);
}

int 
data_received(CONN_STATE *csp, void *data, int len)
{
	if (data == NULL) {
		WLOG_DEBUG("data is NULL!");
		return (CSF_OK);
	}

	((char *)data)[len] = '\0';

	if (csp == NULL) {
		return (do_protocol_session_entry(NULL, NULL, NULL, data, len));
	} else {
		return (do_protocol_session_entry(csp, &(csp->ci),
			csp->data, data, len));
	}
}

int 
connection_lost(CONN_STATE *csp)
{
	CONN_INFO *cip;

	if (csp == NULL) {
		WLOG_DEBUG("csp is NULL!");
		return (CSF_OK);
	}
	cip = &(csp->ci);
	ip2str(cip, ip_str, 16);
	WLOG_INFO("Connection closed, fd: %d, IP: %s", cip->fd, ip_str);

	connection_counter--;
	do_protocol_session_end(cip, csp->data);
	
	return (CSF_OK);
}

