#ifndef _CSF_COMM_PROTO_H
#define _CSF_COMM_PROTO_H

#include <sys/time.h>
#include <event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define SEND_BUF_LEN		1024

typedef struct comm_proto_ops COMM_PROTO_OPS;

typedef struct conn_info {
	int type;
	int fd;
	char data_buf[SEND_BUF_LEN];
	char *data_sent;
	char *data_stored;
	struct sockaddr_in addr; 
	socklen_t addrlen;
	COMM_PROTO_OPS *cp_ops;
} CONN_INFO;

typedef int PROTO_INIT(uint32_t, char *, uint32_t);
typedef void CONN_HANDLER(int, short, void *);
typedef void SOCKET_EVENT_HANDLER(int, short, void *);
typedef void CONN_CLOSE(CONN_INFO *);
typedef int PORT_CLOSE(void);
typedef int SEND_BACK(CONN_INFO *, const void *, const int);
typedef int SOCKET_SENDFILE(CONN_INFO *, int, size_t, off_t *, off_t *);
typedef ssize_t WRITEV_BACK(CONN_INFO *, const struct iovec *, uint16_t, int);
typedef ssize_t WRITE_BACK(CONN_INFO *, const char *, size_t, int);

struct comm_proto_ops {
	const char *proto_name;
	int	sockfd;
	PROTO_INIT *proto_init;
	CONN_HANDLER *conn_handler;
	SOCKET_EVENT_HANDLER *event_handler;
	SEND_BACK *send_back;
	SOCKET_SENDFILE *send_file;
	WRITEV_BACK *writev_back;
	CONN_CLOSE *conn_close;
	PORT_CLOSE *port_close;
	WRITE_BACK *write_back;
};

#endif
