#include <sys/socket.h>
#include <event.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "server.h"
#include "data.h"
//#include "common.h"
#include "utils.h"
#include "log.h"

static uint32_t server_timeout;

static int udp_socket_init(uint32_t, char *, uint32_t);
static void udp_socket_event_handler(int, short, void *);
static int udp_send_back(CONN_INFO *, const void *, const int);
static int udp_port_close(void);

extern COMM_HANDLE *g_comm_handle;

COMM_PROTO_OPS udp_ops = {
	.proto_name = "udp",
	.sockfd = -1,
	.proto_init = udp_socket_init,
	.conn_handler = NULL,
	.send_back = udp_send_back,
	.event_handler = udp_socket_event_handler,
	.conn_close = NULL,
	.port_close = udp_port_close,
	.send_file = NULL,
	.writev_back = NULL

};

static int
udp_send_back(CONN_INFO *cip, const void *buf, const int len)
{
	int rc;

	if (cip->type == CSF_UDP) {
		rc = sendto(cip->fd, buf, len, 0, 
			(struct sockaddr *)&cip->addr, cip->addrlen);
		if (rc < 0) {
			return (-1);
		} else {
			return (rc);
		}
	} else {
		return (-1);
	}
}

static int
udp_port_close(void)
{
	return shutdown(udp_ops.sockfd, SHUT_RDWR);
}


static int 
udp_socket_init(uint32_t port, char *ip, uint32_t timeout)
{
	int		sockfd;
	struct sockaddr_in	servaddr;
	int32_t			flag;
	int				rc;

	server_timeout = timeout;
	
	/* Create UDP socket */
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family 	 = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(ip);
	servaddr.sin_port		 = htons(port);

	flag = 1;
	rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	if (rc < 0) {
		WLOG_ERR("setsockopt() error: %s", strerror(errno));
		close(sockfd);
		return (-1);
	}

	rc = bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	if (rc < 0) {
		WLOG_ERR("bind() error: %s", strerror(errno));
		close(sockfd);
		return (-1);
	}
	udp_ops.sockfd = sockfd;

	return sockfd;
}

static uint8_t	recv_data[RECV_DATA_LEN + 1];

static void 
udp_socket_event_handler(int fd, short event, void *arg)
{
	ssize_t			rc;
	CONN_STATE		*csp;	

    /* avoid compiler warning when WLOG_* is not defined */
    CSF_UNUSED_ARG(event);
    CSF_UNUSED_ARG(arg);

	csp = (CONN_STATE *)smalloc(sizeof(CONN_STATE));
	csp->ci.addrlen = sizeof(struct sockaddr);

	WLOG_DEBUG("udp_socket_event_handler() called fd: %d, event: %d, arg: %p",
			fd, event, arg);

	rc = recvfrom(fd, recv_data, RECV_DATA_LEN, 0,
				(struct sockaddr *) &(csp->ci.addr), &(csp->ci.addrlen));
	if (rc < 0) {
		WLOG_ERR("recvfrom() error: %s", strerror(errno));
		return;
	}
	csp->ci.type = CSF_UDP;
	csp->ci.fd = fd;
	csp->ci.cp_ops = &udp_ops;
	csp->ref_cnt = 1;

	connection_made(csp);

	data_received(csp, recv_data, rc);
	connection_lost(csp);

    /* reference count for free */
	CS_RELE(csp);
}

