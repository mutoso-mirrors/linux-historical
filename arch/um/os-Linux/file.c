/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include "os.h"
#include "user.h"
#include "kern_util.h"

static void copy_stat(struct uml_stat *dst, struct stat64 *src)
{
	*dst = ((struct uml_stat) {
		.ust_dev     = src->st_dev,     /* device */
		.ust_ino     = src->st_ino,     /* inode */
		.ust_mode    = src->st_mode,    /* protection */
		.ust_nlink   = src->st_nlink,   /* number of hard links */
		.ust_uid     = src->st_uid,     /* user ID of owner */
		.ust_gid     = src->st_gid,     /* group ID of owner */
		.ust_size    = src->st_size,    /* total size, in bytes */
		.ust_blksize = src->st_blksize, /* blocksize for filesys I/O */
		.ust_blocks  = src->st_blocks,  /* number of blocks allocated */
		.ust_atime   = src->st_atime,   /* time of last access */
		.ust_mtime   = src->st_mtime,   /* time of last modification */
		.ust_ctime   = src->st_ctime,   /* time of last change */
	});
}

int os_stat_fd(const int fd, struct uml_stat *ubuf)
{
	struct stat64 sbuf;
	int err;

	do {
		err = fstat64(fd, &sbuf);
	} while((err < 0) && (errno == EINTR)) ;

	if(err < 0)
		return(-errno);

	if(ubuf != NULL)
		copy_stat(ubuf, &sbuf);
	return(err);
}

int os_stat_file(const char *file_name, struct uml_stat *ubuf)
{
	struct stat64 sbuf;
	int err;

	do {
		err = stat64(file_name, &sbuf);
	} while((err < 0) && (errno == EINTR)) ;

	if(err < 0)
		return(-errno);

	if(ubuf != NULL)
		copy_stat(ubuf, &sbuf);
	return(err);
}

int os_access(const char* file, int mode)
{
	int amode, err;

	amode=(mode&OS_ACC_R_OK ? R_OK : 0) | (mode&OS_ACC_W_OK ? W_OK : 0) |
	      (mode&OS_ACC_X_OK ? X_OK : 0) | (mode&OS_ACC_F_OK ? F_OK : 0) ;

	err = access(file, amode);
	if(err < 0)
		return(-errno);

	return(0);
}

void os_print_error(int error, const char* str)
{
	errno = error < 0 ? -error : error;

	perror(str);
}

/* FIXME? required only by hostaudio (because it passes ioctls verbatim) */
int os_ioctl_generic(int fd, unsigned int cmd, unsigned long arg)
{
	int err;

	err = ioctl(fd, cmd, arg);
	if(err < 0)
		return(-errno);

	return(err);
}

int os_window_size(int fd, int *rows, int *cols)
{
	struct winsize size;

	if(ioctl(fd, TIOCGWINSZ, &size) < 0)
		return(-errno);

	*rows = size.ws_row;
	*cols = size.ws_col;

	return(0);
}

int os_new_tty_pgrp(int fd, int pid)
{
	if(ioctl(fd, TIOCSCTTY, 0) < 0){
		printk("TIOCSCTTY failed, errno = %d\n", errno);
		return(-errno);
	}

	if(tcsetpgrp(fd, pid) < 0){
		printk("tcsetpgrp failed, errno = %d\n", errno);
		return(-errno);
	}

	return(0);
}

/* FIXME: ensure namebuf in os_get_if_name is big enough */
int os_get_ifname(int fd, char* namebuf)
{
	if(ioctl(fd, SIOCGIFNAME, namebuf) < 0)
		return(-errno);

	return(0);
}

int os_set_slip(int fd)
{
	int disc, sencap;

	disc = N_SLIP;
	if(ioctl(fd, TIOCSETD, &disc) < 0){
		printk("Failed to set slip line discipline - "
		       "errno = %d\n", errno);
		return(-errno);
	}

	sencap = 0;
	if(ioctl(fd, SIOCSIFENCAP, &sencap) < 0){
		printk("Failed to set slip encapsulation - "
		       "errno = %d\n", errno);
		return(-errno);
	}

	return(0);
}

int os_set_owner(int fd, int pid)
{
	if(fcntl(fd, F_SETOWN, pid) < 0){
		int save_errno = errno;

		if(fcntl(fd, F_GETOWN, 0) != pid)
			return(-save_errno);
	}

	return(0);
}

/* FIXME? moved wholesale from sigio_user.c to get fcntls out of that file */
int os_sigio_async(int master, int slave)
{
	int flags;

	flags = fcntl(master, F_GETFL);
	if(flags < 0) {
		printk("fcntl F_GETFL failed, errno = %d\n", errno);
		return(-errno);
	}

	if((fcntl(master, F_SETFL, flags | O_NONBLOCK | O_ASYNC) < 0) ||
	   (fcntl(master, F_SETOWN, os_getpid()) < 0)){
		printk("fcntl F_SETFL or F_SETOWN failed, errno = %d\n",
		       errno);
		return(-errno);
	}

	if((fcntl(slave, F_SETFL, flags | O_NONBLOCK) < 0)){
		printk("fcntl F_SETFL failed, errno = %d\n", errno);
		return(-errno);
	}

	return(0);
}

int os_mode_fd(int fd, int mode)
{
	int err;

	do {
		err = fchmod(fd, mode);
	} while((err < 0) && (errno==EINTR)) ;

	if(err < 0)
		return(-errno);

	return(0);
}

int os_file_type(char *file)
{
	struct uml_stat buf;
	int err;

	err = os_stat_file(file, &buf);
	if(err < 0)
		return(err);

	if(S_ISDIR(buf.ust_mode)) return(OS_TYPE_DIR);
	else if(S_ISLNK(buf.ust_mode)) return(OS_TYPE_SYMLINK);
	else if(S_ISCHR(buf.ust_mode)) return(OS_TYPE_CHARDEV);
	else if(S_ISBLK(buf.ust_mode)) return(OS_TYPE_BLOCKDEV);
	else if(S_ISFIFO(buf.ust_mode)) return(OS_TYPE_FIFO);
	else if(S_ISSOCK(buf.ust_mode)) return(OS_TYPE_SOCK);
	else return(OS_TYPE_FILE);
}

int os_file_mode(char *file, struct openflags *mode_out)
{
	int err;

	*mode_out = OPENFLAGS();

	err = os_access(file, OS_ACC_W_OK);
	if((err < 0) && (err != -EACCES))
		return(err);

	*mode_out = of_write(*mode_out);

	err = os_access(file, OS_ACC_R_OK);
	if((err < 0) && (err != -EACCES))
		return(err);

	*mode_out = of_read(*mode_out);

	return(0);
}

int os_open_file(char *file, struct openflags flags, int mode)
{
	int fd, f = 0;

	if(flags.r && flags.w) f = O_RDWR;
	else if(flags.r) f = O_RDONLY;
	else if(flags.w) f = O_WRONLY;
	else f = 0;

	if(flags.s) f |= O_SYNC;
	if(flags.c) f |= O_CREAT;
	if(flags.t) f |= O_TRUNC;
	if(flags.e) f |= O_EXCL;

	fd = open64(file, f, mode);
	if(fd < 0)
		return(-errno);

	if(flags.cl && fcntl(fd, F_SETFD, 1)){
		os_close_file(fd);
		return(-errno);
	}

	return(fd);
}

int os_connect_socket(char *name)
{
	struct sockaddr_un sock;
	int fd, err;

	sock.sun_family = AF_UNIX;
	snprintf(sock.sun_path, sizeof(sock.sun_path), "%s", name);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd < 0)
		return(fd);

	err = connect(fd, (struct sockaddr *) &sock, sizeof(sock));
	if(err)
		return(-errno);

	return(fd);
}

void os_close_file(int fd)
{
	close(fd);
}

int os_seek_file(int fd, __u64 offset)
{
	__u64 actual;

	actual = lseek64(fd, offset, SEEK_SET);
	if(actual != offset)
		return(-errno);
	return(0);
}

static int fault_buffer(void *start, int len,
			int (*copy_proc)(void *addr, void *buf, int len))
{
	int page = getpagesize(), i;
	char c;

	for(i = 0; i < len; i += page){
		if((*copy_proc)(start + i, &c, sizeof(c)))
			return(-EFAULT);
	}
	if((len % page) != 0){
		if((*copy_proc)(start + len - 1, &c, sizeof(c)))
			return(-EFAULT);
	}
	return(0);
}

static int file_io(int fd, void *buf, int len,
		   int (*io_proc)(int fd, void *buf, int len),
		   int (*copy_user_proc)(void *addr, void *buf, int len))
{
	int n, err;

	do {
		n = (*io_proc)(fd, buf, len);
		if((n < 0) && (errno == EFAULT)){
			err = fault_buffer(buf, len, copy_user_proc);
			if(err)
				return(err);
			n = (*io_proc)(fd, buf, len);
		}
	} while((n < 0) && (errno == EINTR));

	if(n < 0)
		return(-errno);
	return(n);
}

int os_read_file(int fd, void *buf, int len)
{
	return(file_io(fd, buf, len, (int (*)(int, void *, int)) read,
		       copy_from_user_proc));
}

int os_write_file(int fd, const void *buf, int len)
{
	return(file_io(fd, (void *) buf, len,
		       (int (*)(int, void *, int)) write, copy_to_user_proc));
}

int os_file_size(char *file, long long *size_out)
{
	struct uml_stat buf;
	int err;

	err = os_stat_file(file, &buf);
	if(err < 0){
		printk("Couldn't stat \"%s\" : err = %d\n", file, -err);
		return(err);
	}

	if(S_ISBLK(buf.ust_mode)){
		int fd, blocks;

		fd = os_open_file(file, of_read(OPENFLAGS()), 0);
		if(fd < 0){
			printk("Couldn't open \"%s\", errno = %d\n", file, -fd);
			return(fd);
		}
		if(ioctl(fd, BLKGETSIZE, &blocks) < 0){
			printk("Couldn't get the block size of \"%s\", "
			       "errno = %d\n", file, errno);
			err = -errno;
			os_close_file(fd);
			return(err);
		}
		*size_out = ((long long) blocks) * 512;
		os_close_file(fd);
		return(0);
	}
	*size_out = buf.ust_size;
	return(0);
}

int os_file_modtime(char *file, unsigned long *modtime)
{
	struct uml_stat buf;
	int err;

	err = os_stat_file(file, &buf);
	if(err < 0){
		printk("Couldn't stat \"%s\" : err = %d\n", file, -err);
		return(err);
	}

	*modtime = buf.ust_mtime;
	return(0);
}

int os_get_exec_close(int fd, int* close_on_exec)
{
	int ret;

	do {
		ret = fcntl(fd, F_GETFD);
	} while((ret < 0) && (errno == EINTR)) ;

	if(ret < 0)
		return(-errno);

	*close_on_exec = (ret&FD_CLOEXEC) ? 1 : 0;
	return(ret);
}

int os_set_exec_close(int fd, int close_on_exec)
{
	int flag, err;

	if(close_on_exec) flag = FD_CLOEXEC;
	else flag = 0;

	do {
		err = fcntl(fd, F_SETFD, flag);
	} while((err < 0) && (errno == EINTR)) ;

	if(err < 0)
		return(-errno);
	return(err);
}

int os_pipe(int *fds, int stream, int close_on_exec)
{
	int err, type = stream ? SOCK_STREAM : SOCK_DGRAM;

	err = socketpair(AF_UNIX, type, 0, fds);
	if(err < 0)
		return(-errno);

	if(!close_on_exec)
		return(0);

	err = os_set_exec_close(fds[0], 1);
	if(err < 0)
		goto error;

	err = os_set_exec_close(fds[1], 1);
	if(err < 0)
		goto error;

	return(0);

 error:
	printk("os_pipe : Setting FD_CLOEXEC failed, err = %d\n", -err);
	os_close_file(fds[1]);
	os_close_file(fds[0]);
	return(err);
}

int os_set_fd_async(int fd, int owner)
{
	/* XXX This should do F_GETFL first */
	if(fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK) < 0){
		printk("os_set_fd_async : failed to set O_ASYNC and "
		       "O_NONBLOCK on fd # %d, errno = %d\n", fd, errno);
		return(-errno);
	}
#ifdef notdef
	if(fcntl(fd, F_SETFD, 1) < 0){
		printk("os_set_fd_async : Setting FD_CLOEXEC failed, "
		       "errno = %d\n", errno);
	}
#endif

	if((fcntl(fd, F_SETSIG, SIGIO) < 0) ||
	   (fcntl(fd, F_SETOWN, owner) < 0)){
		printk("os_set_fd_async : Failed to fcntl F_SETOWN "
		       "(or F_SETSIG) fd %d to pid %d, errno = %d\n", fd, 
		       owner, errno);
		return(-errno);
	}

	return(0);
}

int os_clear_fd_async(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	flags &= ~(O_ASYNC | O_NONBLOCK);
	if(fcntl(fd, F_SETFL, flags) < 0)
		return(-errno);
	return(0);
}

int os_set_fd_block(int fd, int blocking)
{
	int flags;

	flags = fcntl(fd, F_GETFL);

	if(blocking) flags &= ~O_NONBLOCK;
	else flags |= O_NONBLOCK;

	if(fcntl(fd, F_SETFL, flags) < 0){
		printk("Failed to change blocking on fd # %d, errno = %d\n",
		       fd, errno);
		return(-errno);
	}
	return(0);
}

int os_accept_connection(int fd)
{
	int new;

	new = accept(fd, NULL, 0);
	if(new < 0) 
		return(-errno);
	return(new);
}

#ifndef SHUT_RD
#define SHUT_RD 0
#endif

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

int os_shutdown_socket(int fd, int r, int w)
{
	int what, err;

	if(r && w) what = SHUT_RDWR;
	else if(r) what = SHUT_RD;
	else if(w) what = SHUT_WR;
	else {
		printk("os_shutdown_socket : neither r or w was set\n");
		return(-EINVAL);
	}
	err = shutdown(fd, what);
	if(err < 0)
		return(-errno);
	return(0);
}

int os_rcv_fd(int fd, int *helper_pid_out)
{
	int new, n;
	char buf[CMSG_SPACE(sizeof(new))];
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	iov = ((struct iovec) { .iov_base  = helper_pid_out,
				.iov_len   = sizeof(*helper_pid_out) });
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_flags = 0;

	n = recvmsg(fd, &msg, 0);
	if(n < 0)
		return(-errno);

	else if(n != sizeof(iov.iov_len))
		*helper_pid_out = -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == NULL){
		printk("rcv_fd didn't receive anything, error = %d\n", errno);
		return(-1);
	}
	if((cmsg->cmsg_level != SOL_SOCKET) || 
	   (cmsg->cmsg_type != SCM_RIGHTS)){
		printk("rcv_fd didn't receive a descriptor\n");
		return(-1);
	}

	new = ((int *) CMSG_DATA(cmsg))[0];
	return(new);
}

int os_create_unix_socket(char *file, int len, int close_on_exec)
{
	struct sockaddr_un addr;
	int sock, err;

	sock = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0){
		printk("create_unix_socket - socket failed, errno = %d\n",
		       errno);
		return(-errno);
	}

	if(close_on_exec) {
		err = os_set_exec_close(sock, 1);
		if(err < 0)
			printk("create_unix_socket : close_on_exec failed, "
		       "err = %d", -err);
	}

	addr.sun_family = AF_UNIX;

	/* XXX Be more careful about overflow */
	snprintf(addr.sun_path, len, "%s", file);

	err = bind(sock, (struct sockaddr *) &addr, sizeof(addr));
	if (err < 0){
		printk("create_listening_socket at '%s' - bind failed, "
		       "errno = %d\n", file, errno);
		return(-errno);
	}

	return(sock);
}

void os_flush_stdout(void)
{
	fflush(stdout);
}

int os_lock_file(int fd, int excl)
{
	int type = excl ? F_WRLCK : F_RDLCK;
	struct flock lock = ((struct flock) { .l_type	= type,
					      .l_whence	= SEEK_SET,
					      .l_start	= 0,
					      .l_len	= 0 } );
	int err, save;

	err = fcntl(fd, F_SETLK, &lock);
	if(!err)
		goto out;

	save = -errno;
	err = fcntl(fd, F_GETLK, &lock);
	if(err){
		err = -errno;
		goto out;
	}

	printk("F_SETLK failed, file already locked by pid %d\n", lock.l_pid);
	err = save;
 out:
	return(err);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
