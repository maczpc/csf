#include <sys/socket.h>
#include <event.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <poll.h>
#ifdef LINUX_API
#include <sys/sendfile.h>
#endif


#include "server.h"
#include "data.h"
//#include "common.h"
#include "utils.h"
#include "log.h"
#include "protocol.h"

#ifndef POLLWRNORM
#define	POLLWRNORM	POLLOUT
#endif

#ifdef	POLLRDNORM
#define POLLRDNORM	POLLIN
#endif

static uint32_t server_timeout;

static int tcp_socket_init(uint32_t, char *, uint32_t);
static void tcp_conn_close(CONN_INFO *);
static void tcp_conn_handler(int, short, void *);
static void tcp_socket_event_handler(int, short, void *);
static int tcp_send_back(CONN_INFO *, const void *, const int);
static void tcp_send_back_handler(int, short, void *);
static void tcp_event_handler(int, short, void *);

static int tcp_sendfile(CONN_INFO *, int, size_t, off_t *, off_t *);
static ssize_t tcp_writev(CONN_INFO *, const struct iovec *, uint16_t, int);
static ssize_t tcp_write(CONN_INFO *, const char *, size_t, int);
static int tcp_port_close(void);


COMM_PROTO_OPS tcp_ops = {
	.proto_name = "tcp",
	.sockfd = -1,
	.proto_init = tcp_socket_init,
	.conn_handler = tcp_conn_handler,
	.event_handler = tcp_socket_event_handler,
	.send_back = tcp_send_back,
	.conn_close = tcp_conn_close,
	.port_close = tcp_port_close,
	.send_file = tcp_sendfile,
	.writev_back = tcp_writev,
    .write_back = tcp_write
};


/* Max. sendfile block size (bytes), limiting size of data sent by
 * each sendfile call may: improve responsiveness, reduce memory
 * pressure, prevent blocking calls, etc.
 * 
 * NOTE: try to reduce block size (down to 64 KB) on small or old
 * systems and see if something improves under heavy load.
 */
#define MAX_SF_BLK_SIZE		(65536 * 16)	          /* limit size of block size */
#define MAX_SF_BLK_SIZE2	(MAX_SF_BLK_SIZE + 65536) /* upper limit */

#ifndef IOV_MAX
#define IOV_MAX 1024
#endif


static ssize_t 
tcp_write(CONN_INFO *cip, const char *buf, size_t len, int timeout)
{
    size_t nwritten = 0;
    ssize_t rv;
    int err;
    int nready;
    struct pollfd pollfd;

    while (nwritten < len) {
        rv = write(cip->fd, buf, len);

        if (rv < 0) {
            err = errno;
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
            if (err == EWOULDBLOCK) {
                pollfd.fd = cip->fd;
                pollfd.events = POLLWRNORM;
                nready = poll(&pollfd, 1, timeout);
				if (nready == 0) {	
					WLOG_INFO("Poll timeout.");
					return -1;
				}
                continue;
            }
#endif
            if (err == EAGAIN) {
                pollfd.fd = cip->fd;
                pollfd.events = POLLWRNORM;
                nready = poll(&pollfd, 1, timeout);
				if (nready == 0) {	
					WLOG_INFO("Poll timeout.");
					return -1;
				}
                continue;
            } else if (err == EINTR) {
                continue;
            } else {
                WLOG_ERR("write error. errno:%d", err);
                return -1;
            }
        }
        nwritten += rv;
    }
    return (ssize_t)nwritten;
}

static ssize_t
tcp_writev(CONN_INFO *cip, const struct iovec *vector, 
            uint16_t vector_len, int timeout)
{
    int i, nready;
    int cur_block = 0;
    size_t cur_offset = 0;
    uint32_t cur_countbytes = 0;
    ssize_t nwritten = 0;
    ssize_t rc;
    int err;
    int write_again = 1;
    struct pollfd pollfd;
    struct iovec new_vector[IOV_MAX];
    struct iovec *new_vector_startp = NULL;
    uint16_t new_vector_len = 0;
	ssize_t vec_base_byte_ordinal[IOV_MAX];

	if (vector == NULL || vector_len <= 0 || vector_len > IOV_MAX) {
		return -1;
	}

    /* copy the const struct to a non-const struct of iovec 
	 * and then compute the byte ordinal of each vector base */
    for (i = 0; i < vector_len; i++) {
        new_vector[i].iov_base = vector[i].iov_base;
        new_vector[i].iov_len = vector[i].iov_len;

		vec_base_byte_ordinal[i] = 0;
		if (i - 1 >= 0) {
			vec_base_byte_ordinal[i] += 
				(vec_base_byte_ordinal[i - 1] + vector[i].iov_len);
		} else {
			vec_base_byte_ordinal[i] += vector[i].iov_len;
		}
    }

    /* now we writev until all data has been written */
	while (cur_block < vector_len) {
        /* proccess the situation of part of the iov_base be sent */
        if (write_again == 1) {
            new_vector_startp = &(new_vector[cur_block]);
            new_vector_len = vector_len - cur_block;
            new_vector[cur_block].iov_base = (void *)((char *)new_vector[cur_block].iov_base + cur_offset);
	    	new_vector[cur_block].iov_len = new_vector[cur_block].iov_len - cur_offset;
        }

        write_again = 1;
        rc = writev(cip->fd, new_vector_startp, new_vector_len);

        if (rc < 0) {
            err = errno;
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
            if (err == EWOULDBLOCK) {
                pollfd.fd = cip->fd;
                pollfd.events = POLLWRNORM;
                nready = poll(&pollfd, 1, timeout);
				if (nready == 0) {	
					WLOG_INFO("Poll timeout.");
					return -1;
				}
                write_again = 0;
                continue;
            }
#endif
            if (err == EAGAIN) {
                pollfd.fd = cip->fd;
                pollfd.events = POLLWRNORM;
                nready = poll(&pollfd, 1, timeout);
				if (nready == 0) {	
					WLOG_INFO("Poll timeout.");
					return -1;
				}
                write_again = 0;
                continue;
            } else if (err == EINTR) {
                write_again = 0;
                continue;
            } else {
                WLOG_ERR("ERROR.IOVCNT:%d,errno:%d", new_vector_len, errno);
                return -1;
            }
        }

        /* search block and offset by written bytes */
        nwritten += rc;
		cur_block= 0;
		cur_countbytes = 0;
		cur_offset = 0;
		for (i = 0; i < vector_len; i++) {
			if (nwritten < vec_base_byte_ordinal[i]) {
				cur_offset = vector[i].iov_len - (vec_base_byte_ordinal[i] - nwritten);
				break;
			} else if (nwritten == vec_base_byte_ordinal[i]) {
				cur_block++;
				cur_offset = 0;
				break;
			} else {
				cur_block++;
			}
		}
    }
	return nwritten;
}


static int 
tcp_sendfile (CONN_INFO *cip,
			  int      fd, 
			  size_t   size, 
			  off_t   *offset, 
			  off_t *sent)
{
	int		re;


 	/* If there is nothing to send then return now, this may be
 	 * needed in some systems (i.e. *BSD) because value 0 may have
 	 * special meanings or trigger occasional hidden bugs.
 	 */
	if (size == 0)
		return (0);

	/* Limit size of data that has to be sent.
	 */
	if (size > MAX_SF_BLK_SIZE2)
		size = MAX_SF_BLK_SIZE;

#if defined(LINUX_API)

	/* Linux sendfile
	 *
	 * ssize_t 
	 * sendfile (int out_fd, int in_fd, off_t *offset, size_t *count);
	 *
	 * ssize_t 
	 * sendfile64 (int out_fd, int in_fd, off64_t *offset, size_t *count);
	 */
	*sent = sendfile(cip->fd,     /* int     out_fd */
			  fd,                    /* int     in_fd  */
			  offset,                /* off_t  *offset */
			  size);                 /* size_t  count  */
		
	if (*sent < 0) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
		case EWOULDBLOCK:
#endif
		case EINVAL:
			/* maybe sendfile is not supported by this FS (no mmap available),
			 * since more than one FS can be used (ext2, ext3, ntfs, etc.)
			 * we should retry with emulated sendfile (read+write).
			 */

		case ENOSYS: 
			/* This kernel does not support sendfile at all.
			 */
			return (-1);
		}

		return (-1);

	} else if (*sent == 0) {
		/* It isn't an error, but it wrote nothing */
		return (-1);
	}

#elif SOLARIS_API

	*sent = sendfile (cip->fd,     /* int   out_fd */
			  fd,                    /* int    in_fd */
			  offset,                /* off_t   *off */
			  size);                 /* size_t   len */

	if (*sent < 0) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
		case EWOULDBLOCK:
#endif
		case EINVAL:
			/* maybe sendfile is not supported by this FS (no mmap available),
			 * since more than one FS can be used (ext2, ext3, ntfs, etc.)
			 * we should retry with emulated sendfile (read+write).
			 */

		case ENOSYS: 
			/* This kernel does not support sendfile at all.
			 */
			return (-1);
		}

		return (-1);

	} else if (*sent == 0) {
		/* It isn't an error, but it wrote nothing */
		return (-1);
	}

#elif FREEBSD_API
	struct sf_hdtr hdr;
	struct iovec   hdtrl;

	hdr.headers    = &hdtrl;
	hdr.hdr_cnt    = 1;
	hdr.trailers   = NULL;
	hdr.trl_cnt    = 0;
	
	hdtrl.iov_base = NULL;
	hdtrl.iov_len  = 0;

	*sent = 0;

	/* FreeBSD sendfile: in_fd and out_fd are reversed
	 *
	 * int
	 * sendfile (int fd, int s, off_t offset, size_t nbytes,
	 *           struct sf_hdtr *hdtr, off_t *sbytes, int flags);
	 */	
	re = sendfile(fd,                        /* int             fd     */
		       cip->fd,         /* int             s      */
		       *offset,                   /* off_t           offset */
		       size,                      /* size_t          nbytes */
		       &hdr,                      /* struct sf_hdtr *hdtr   */
		       sent,                      /* off_t          *sbytes */ 
		       0);                        /* int             flags  */

	if (re == -1) {
		switch(errno) {
		case EINTR:
		case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
		case EWOULDBLOCK:
#endif
			if(*sent < 1)
				return (-1);

			/* else it's ok, something has been sent.
			 */
			break;
		case ENOSYS:
			return (-1);
		default:
			return (-1);
		}

	} else if (*sent == 0) {
		/* It isn't an error, but it wrote nothing */
		return (-1);
	}

	*offset = *offset + *sent;
#elif MACOS_API
	/* MacOS X: BSD-like System Call
	 *
	 * int
	 * sendfile (int fd, int s, off_t offset, off_t *len,
	 *           struct sf_hdtr *hdtr, int flags);
	 */	
	re = sendfile (fd,                        /* int             fd     */
		       cip->fd,         /* int             s      */
		       *offset,                   /* off_t           offset */
		       &sent,                    /* off_t          *len    */
		       NULL,                      /* struct sf_hdtr *hdtr   */
		       0);                        /* int             flags  */
	if (re == -1) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
			/* It might have sent some information
			 */
			if (sent <= 0)
				return (-1);
			break;
		case ENOSYS:
			return (-1);
		default:
			return (-1);
		}

	} else if (sent == 0) {
		/* It isn't an error, but it wrote nothing */
		return (-1);
	}
	*offset = *offset + *sent;

#else
	CSF_UNUSED_ARG(cip);
	CSF_UNUSED_ARG(fd);
	CSF_UNUSED_ARG(offset);
	CSF_UNUSED_ARG(sent);

	return (-1);
#endif

	return (0);
}


static void 
tcp_event_handler(int fd, short event, void *arg)
{
    if (event == EV_READ)
        tcp_conn_handler(fd, event, arg);
    else if (event == EV_WRITE)
        tcp_send_back_handler(fd, event, arg);
    else
        tcp_conn_handler(fd, event, arg);
}

static void 
tcp_send_back_handler(int fd, short event, void *arg)
{
	ssize_t	nleft;	
	ssize_t	nwritten;
	CONN_INFO *cip;
    CONN_STATE *csp;
    struct timeval	tv;

	CSF_UNUSED_ARG(event);
	csp = (CONN_STATE *)arg;
	cip = &(csp->ci);

    tv.tv_sec = server_timeout;
	tv.tv_usec = 0;	

	while ((nleft = (cip->data_stored - cip->data_sent)) > 0) {
		nwritten = write(fd, cip->data_sent, nleft);
		if (nwritten < 0) {
			if (errno == EINTR) {
				nwritten = 0;
			} else if (errno == EWOULDBLOCK) {
				WLOG_NOTICE("send back is blocked. waiting for writing available.");
                event_add(&(csp->ev), &tv);
				return;
			} else {
				WLOG_ERR("Unknown send back error. errno:%d.", errno);
				break;
			}
		}
		cip->data_sent += nwritten;
	}
	
	cip->data_buf[0] = 0;
	cip->data_sent = cip->data_buf;
	cip->data_stored = cip->data_buf;
	
    event_del(&(csp->ev));
    event_set(&(csp->ev), cip->fd, EV_READ, tcp_event_handler, csp);
            
	if(event_add(&(csp->ev), &tv) < 0) {
		WLOG_ERR("Add event error: %s", strerror(errno));
		return;
	} 
}


static int
tcp_send_back(CONN_INFO *cip, const void *buf, const int len)
{
	ssize_t data_freelen;
	ssize_t data_len;
	ssize_t	nwritten;
	const char *ptr;
    CONN_STATE *csp;
    struct timeval	tv;

	ptr = buf;
	data_len = len;

	if (cip->type != CSF_TCP)
		return -1;

	if (cip->data_stored == cip->data_buf) {
		nwritten = write(cip->fd, ptr, data_len);
		if (nwritten < 0) {
			if (errno == EINTR) {
				nwritten = 0;
			} else if (errno != EWOULDBLOCK) {
				WLOG_ERR("Unknown send back error. errno:%d.", errno);
				return -1;
			}
		} else {
			ptr += nwritten;
			data_len -= nwritten;
			if (data_len == 0) {
				return 0;
			}
		}
	}
		
	data_freelen = SEND_BUF_LEN - (cip->data_stored - cip->data_buf);

	if (data_freelen >= data_len) {
		if (cip->data_stored == cip->data_buf) {
			/* delete the old event and then add a new event */
            
            csp = GET_ENTRY(cip, CONN_STATE, ci);
            event_del(&(csp->ev));
            event_set(&(csp->ev), cip->fd, EV_READ | EV_WRITE, tcp_event_handler, csp);
            
            tv.tv_sec = server_timeout;
	        tv.tv_usec = 0;			

            if(event_add(&(csp->ev), &tv) < 0) {
				WLOG_ERR("Add event error: %s", strerror(errno));
				return -1;
			}   
		}
		memcpy(cip->data_stored, ptr, data_len);
		cip->data_stored += data_len;
	} else {
		return -1;
	}
	
	return 0;
}

static int 
tcp_socket_init(uint32_t port, char *ip, uint32_t timeout)
{
	int		sockfd;
	struct sockaddr_in	servaddr;
	int32_t			flag;
	int				retval;

	server_timeout = timeout;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		WLOG_ERR("socket create error in %s", __func__);
		return sockfd;
	} else {
		WLOG_INFO("listening socket %d is created", sockfd);
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family			= AF_INET;
	servaddr.sin_addr.s_addr	= inet_addr(ip);
	servaddr.sin_port			= htons(port);

	flag = 1;
	retval = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	if (retval < 0) {
		WLOG_ERR("setsockopt error: %s", strerror(errno));
		close(sockfd);
		return (-1);
	}

	retval = bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if (retval < 0) {
		WLOG_ERR("bind error: %s", strerror(errno));
		close(sockfd);
		return (-1);
	}

	retval = listen(sockfd, 1024);
	if (retval < 0) {
		WLOG_ERR("listen error: %s", strerror(errno));
		close(sockfd);
		return (-1);
	} else {
		WLOG_INFO("start listening from socket %d", sockfd);
	}
	tcp_ops.sockfd = sockfd;
	
	return sockfd;
}

static void 
tcp_conn_close(CONN_INFO *cip)
{
	if (cip == NULL) {
		WLOG_ERR("Is csp connection freed?");
		return;
	}

	if (cip->type != CSF_TCP) {
		return;
	}

	close(cip->fd);

	/* We set the fd = -1, to avoid anyone who would use it in thread */
	cip->fd = -1;
}

static int
tcp_port_close(void)
{
	return shutdown(tcp_ops.sockfd, SHUT_RDWR);
}

static uint8_t	recv_data[RECV_DATA_LEN + 1];

static void 
tcp_conn_handler(int fd, short event, void *arg)
{
	ssize_t			len;
	int				val;
	int				rc;
	CONN_STATE		*csp = (CONN_STATE *)arg;
	struct timeval	tv;

	if (event == EV_TIMEOUT) {
		WLOG_INFO("Timeout! Connection lost!");
		tcp_conn_close(&(csp->ci));
		conn_state_cleanup(csp);
		return;
	}
	
	val = fcntl(fd, F_GETFL, 0);
	if(val < 0) {
		WLOG_ERR(strerror(errno));
		return;
	}
	(void)fcntl(fd, F_SETFL, val | O_NONBLOCK);

	for (;;) {
		len = read (fd, recv_data, RECV_DATA_LEN);

		if (len == -1) {
			if (errno != EWOULDBLOCK || errno != EAGAIN) {
				WLOG_ERR("read() error! fd: %d, errno: %d", fd, errno);
				if (errno == ECONNRESET) {
					tcp_conn_close(&(csp->ci));
					conn_state_cleanup(csp);
				}
				return;
			}
			/* Just break, until the next read request */
			break;
		} else if (len == 0) {
			/* Got EOF, Connection closed */
			tcp_conn_close(&(csp->ci));
			conn_state_cleanup(csp);
			return;
		} else {
			recv_data[len] = '\0';
			rc = data_received(csp, recv_data, len);

			if (rc == PROTOCOL_DISCONNECT) {
				tcp_conn_close(&(csp->ci));
				conn_state_cleanup(csp);
				return;
			}

			if (rc == PROTOCOL_ERROR) {
				/* XXX nothing to do? */
			}
			continue;
		}
	}
	
	tv.tv_sec = server_timeout;
	tv.tv_usec = 0;
	(void)event_add(&(csp->ev), &tv);
	return;
}

static void 
tcp_socket_event_handler(int fd, short event, void *arg)
{
	socklen_t		clilen;
	struct sockaddr_in	cliaddr;
	int			connfd;
	CONN_STATE		*csp = NULL;
	struct timeval		tv;
	int					retval;

	/* avoid compiler warning when WLOG_* is not defined */
	CSF_UNUSED_ARG(event);
	CSF_UNUSED_ARG(arg);

	clilen = sizeof(cliaddr);

	/* set fd to avoid the accept blocked */
	retval = fcntl(fd, F_GETFL, 0);
	if(retval < 0) {
		WLOG_ERR("fcntl() error: %s", strerror(errno));
		return;
	}
	(void)fcntl(fd, F_SETFL, retval | O_NONBLOCK);
	while (1) {
		connfd = accept(fd, (struct sockaddr *) &cliaddr, &clilen); 
		if(connfd < 0) {
			WLOG_WARNING("accept() error: %s", strerror(errno));
			switch (errno) {
				case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
				case EWOULDBLOCK:
#endif

				case EINTR:
					continue;
				case ECONNABORTED:
					/* XXX We should do something to handle the error */
					return;
				default:
					return;
				
			}
		}
		break;
	}

	retval = fcntl(connfd, F_GETFL, 0);
	if(retval < 0) {
		WLOG_ERR("fcntl() error: %s", strerror(errno));
		return;
	}
	(void)fcntl(connfd, F_SETFL, retval | O_NONBLOCK);

	csp = (CONN_STATE *)smalloc(sizeof(CONN_STATE));

	csp->ref_cnt = 1;
	csp->ci.fd = connfd;
	csp->ci.type = CSF_TCP;
	csp->ci.cp_ops = &tcp_ops;
	csp->ci.data_buf[0] = 0;
	csp->ci.data_sent = csp->ci.data_buf;
	csp->ci.data_stored = csp->ci.data_buf;

	memcpy(&(csp->ci.addr), &cliaddr, sizeof(struct sockaddr_in));
	memcpy(&(csp->ci.addrlen), &clilen, sizeof(socklen_t));

	/* Process the data when the connection is made */
	(void)connection_made(csp);

	/* Add the new connection event to event loop */
	event_set(&(csp->ev), connfd, EV_READ, tcp_conn_handler, csp);

	tv.tv_sec = server_timeout;
	tv.tv_usec = 0;

	retval = event_add(&(csp->ev), &tv);
	if(retval < 0) {
		WLOG_ERR("Add event error: %s", strerror(errno));
	}
	
	return; 
}

