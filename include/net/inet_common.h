#ifndef _INET_COMMON_H
#define _INET_COMMON_H

extern struct proto_ops		inet_stream_ops;
extern struct proto_ops		inet_dgram_ops;

/*
 *	INET4 prototypes used by INET6
 */

extern void			inet_remove_sock(struct sock *sk1);
extern void			inet_put_sock(unsigned short num, 
					      struct sock *sk);
extern int			inet_release(struct socket *sock);
extern int			inet_stream_connect(struct socket *sock,
						    struct sockaddr * uaddr,
						    int addr_len, int flags);
extern int			inet_dgram_connect(struct socket *sock, 
						   struct sockaddr * uaddr,
						   int addr_len, int flags);
extern int			inet_accept(struct socket *sock, 
					    struct socket *newsock, int flags);
extern int			inet_recvmsg(struct kiocb *iocb,
					     struct socket *sock, 
					     struct msghdr *ubuf, 
					     size_t size, int flags);
extern int			inet_sendmsg(struct kiocb *iocb,
					     struct socket *sock, 
					     struct msghdr *msg, 
					     size_t size);
extern int			inet_shutdown(struct socket *sock, int how);
extern unsigned int		inet_poll(struct file * file, struct socket *sock, struct poll_table_struct *wait);
extern int			inet_setsockopt(struct socket *sock, int level,
						int optname, char *optval, 
						int optlen);
extern int			inet_getsockopt(struct socket *sock, int level,
						int optname, char *optval, 
						int *optlen);
extern int			inet_listen(struct socket *sock, int backlog);

extern void			inet_sock_release(struct sock *sk);
extern void			inet_sock_destruct(struct sock *sk);
extern atomic_t			inet_sock_nr;

extern int			inet_bind(struct socket *sock, 
					  struct sockaddr *uaddr, int addr_len);
extern int			inet_getname(struct socket *sock, 
					     struct sockaddr *uaddr, 
					     int *uaddr_len, int peer);
extern int			inet_ioctl(struct socket *sock, 
					   unsigned int cmd, unsigned long arg);

#endif


