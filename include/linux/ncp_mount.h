/*
 *  ncp_mount.h
 *
 *  Copyright (C) 1995 by Volker Lendecke
 *
 */

#ifndef _LINUX_NCP_MOUNT_H
#define _LINUX_NCP_MOUNT_H

#include <linux/types.h>
#include <linux/ipx.h>
#include <linux/ncp.h>
#include <linux/ncp_fs_i.h>

#define NCP_MOUNT_VERSION 1

#define NCP_USERNAME_LEN (NCP_BINDERY_NAME_LEN)
#define NCP_PASSWORD_LEN 20

/* Values for flags */
#define NCP_MOUNT_SOFT 0x0001
#define NCP_MOUNT_INTR 0x0002

struct ncp_mount_data {
	int version;
	unsigned int ncp_fd;	/* The socket to the ncp port */
	unsigned int wdog_fd;	/* Watchdog packets come here */
	unsigned int message_fd; /* Not used yet, maybe for messages */
        uid_t mounted_uid;      /* Who may umount() this filesystem? */

	struct sockaddr_ipx serv_addr;
	unsigned char server_name[49];

	unsigned char username[NCP_USERNAME_LEN+1];
	unsigned char password[NCP_PASSWORD_LEN+1];

	unsigned int time_out;	/* How long should I wait after
				   sending a NCP request? */
	unsigned int retry_count; /* And how often should I retry? */
	unsigned int flags;

        uid_t uid;
        gid_t gid;
        mode_t file_mode;
        mode_t dir_mode;
};

#endif
