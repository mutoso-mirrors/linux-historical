#ifndef _SCSI_PRIV_H
#define _SCSI_PRIV_H

/*
 * These are the values that the owner field can take.
 * They are used as an indication of who the command belongs to.
 */
#define SCSI_OWNER_HIGHLEVEL      0x100
#define SCSI_OWNER_MIDLEVEL       0x101
#define SCSI_OWNER_LOWLEVEL       0x102
#define SCSI_OWNER_ERROR_HANDLER  0x103
#define SCSI_OWNER_BH_HANDLER     0x104
#define SCSI_OWNER_NOBODY         0x105

/*
 * Magic values for certain scsi structs. Shouldn't ever be used.
 */
#define SCSI_CMND_MAGIC		0xE25C23A5
#define SCSI_REQ_MAGIC		0x75F6D354

/*
 *  Flag bit for the internal_timeout array
 */
#define NORMAL_TIMEOUT		0

/*
 * Scsi Error Handler Flags
 */
#define scsi_eh_eflags_chk(scp, flags) \
	((scp)->eh_eflags & (flags))
#define scsi_eh_eflags_set(scp, flags) \
	do { (scp)->eh_eflags |= (flags); } while(0)
#define scsi_eh_eflags_clr(scp, flags) \
	do { (scp)->eh_eflags &= ~(flags); } while(0)
#define scsi_eh_eflags_clr_all(scp) \
	(scp->eh_eflags = 0)

#define SCSI_EH_CANCEL_CMD	0x0001	/* Cancel this cmd */
#define SCSI_EH_REC_TIMEOUT	0x0002	/* EH retry timed out */

#define SCSI_SENSE_VALID(scmd) \
	(((scmd)->sense_buffer[0] & 0x70) == 0x70)


/*
 * scsi_target: representation of a scsi target, for now, this is only
 * used for single_lun devices. If no one has active IO to the target,
 * starget_sdev_user is NULL, else it points to the active sdev.
 */
struct scsi_target {
	struct scsi_device	*starget_sdev_user;
	unsigned int		starget_refcnt;
};


/* hosts.c */
extern void scsi_host_busy_inc(struct Scsi_Host *, Scsi_Device *);
extern void scsi_host_busy_dec_and_test(struct Scsi_Host *, Scsi_Device *);
extern struct Scsi_Host *scsi_host_get_next(struct Scsi_Host *);
extern void scsi_host_init(void);

/* scsi.c */
extern int scsi_dispatch_cmd(struct scsi_cmnd *cmd);
extern int scsi_setup_command_freelist(struct Scsi_Host *shost);
extern void scsi_destroy_command_freelist(struct Scsi_Host *shost);
extern void scsi_done(struct scsi_cmnd *cmd);
extern void scsi_finish_command(struct scsi_cmnd *cmd);
extern int scsi_retry_command(struct scsi_cmnd *cmd);
extern int scsi_attach_device(struct scsi_device *sdev);
extern void scsi_detach_device(struct scsi_device *sdev);
extern void scsi_rescan_device(struct scsi_device *sdev);
extern int scsi_get_device_flags(unsigned char *vendor, unsigned char *model);
extern int scsi_insert_special_req(struct scsi_request *sreq, int);
extern void scsi_init_cmd_from_req(struct scsi_cmnd *cmd,
		struct scsi_request *sreq);
extern void __scsi_release_request(struct scsi_request *sreq);

/* scsi_error.c */
extern void scsi_times_out(struct scsi_cmnd *cmd);
extern void scsi_error_handler(void *host);
extern int scsi_decide_disposition(struct scsi_cmnd *cmd);
extern int scsi_eh_scmd_add(struct scsi_cmnd *, int);

/* scsi_lib.c */
extern int scsi_maybe_unblock_host(struct scsi_device *sdev);
extern void scsi_setup_cmd_retry(struct scsi_cmnd *cmd);
extern int scsi_queue_insert(struct scsi_cmnd *cmd, int reason);
extern void scsi_next_command(struct scsi_cmnd *cmd);
extern void scsi_run_host_queues(struct Scsi_Host *shost);
extern struct request_queue *scsi_alloc_queue(struct scsi_device *sdev);
extern void scsi_free_queue(struct request_queue *q);
extern int scsi_init_queue(void);
extern void scsi_exit_queue(void);

/* scsi_proc.c */
#ifdef CONFIG_PROC_FS
extern void scsi_proc_host_add(struct Scsi_Host *);
extern void scsi_proc_host_rm(struct Scsi_Host *);
extern int scsi_init_procfs(void);
extern void scsi_exit_procfs(void);
#else
# define scsi_proc_host_add(shost)	do { } while (0)
# define scsi_proc_host_rm(shost)	do { } while (0)
# define scsi_init_procfs()		(0)
# define scsi_exit_procfs		do { } while (0)
#endif /* CONFIG_PROC_FS */

/* scsi_scan.c */
extern void scsi_scan_host(struct Scsi_Host *shost);
extern void scsi_forget_host(struct Scsi_Host *shost);
extern void scsi_free_sdev(struct scsi_device *);

/* scsi_sysfs.c */
extern int scsi_device_register(struct scsi_device *);
extern void scsi_device_unregister(struct scsi_device *);
extern int scsi_upper_driver_register(struct Scsi_Device_Template *);
extern void scsi_upper_driver_unregister(struct Scsi_Device_Template *);
extern int scsi_sysfs_add_host(struct Scsi_Host *, struct device *);
extern void scsi_sysfs_remove_host(struct Scsi_Host *);
extern int scsi_sysfs_register(void);
extern void scsi_sysfs_unregister(void);

/*
 * dev_info: for the black/white list in the old scsi_static_device_list
 */
struct dev_info {
	char *vendor;
	char *model;
	char *revision;	/* revision known to be bad, unused */
	unsigned flags;
};

extern struct dev_info scsi_static_device_list[];

/*
 * scsi_dev_info_list: structure to hold black/white listed devices.
 */
struct scsi_dev_info_list {
	struct list_head dev_info_list;
	char vendor[8];
	char model[16];
	unsigned flags;
	unsigned compatible; /* for use with scsi_static_device_list entries */
};

extern struct list_head scsi_dev_info_list;
extern int scsi_dev_info_list_add_str(char *);

#endif /* _SCSI_PRIV_H */
