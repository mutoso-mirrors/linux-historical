/* This file is automatically generated.  Do not edit. */
/* FLASK */

struct av_perm_to_string
{
    u16 tclass;
    u32 value;
    char *name;
};

static struct av_perm_to_string av_perm_to_string[] = {
   { SECCLASS_FILESYSTEM, FILESYSTEM__MOUNT, "mount" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__REMOUNT, "remount" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__UNMOUNT, "unmount" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__GETATTR, "getattr" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__RELABELFROM, "relabelfrom" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__RELABELTO, "relabelto" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__TRANSITION, "transition" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__ASSOCIATE, "associate" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__QUOTAMOD, "quotamod" },
   { SECCLASS_FILESYSTEM, FILESYSTEM__QUOTAGET, "quotaget" },
   { SECCLASS_DIR, DIR__ADD_NAME, "add_name" },
   { SECCLASS_DIR, DIR__REMOVE_NAME, "remove_name" },
   { SECCLASS_DIR, DIR__REPARENT, "reparent" },
   { SECCLASS_DIR, DIR__SEARCH, "search" },
   { SECCLASS_DIR, DIR__RMDIR, "rmdir" },
   { SECCLASS_FILE, FILE__EXECUTE_NO_TRANS, "execute_no_trans" },
   { SECCLASS_FILE, FILE__ENTRYPOINT, "entrypoint" },
   { SECCLASS_FD, FD__USE, "use" },
   { SECCLASS_TCP_SOCKET, TCP_SOCKET__CONNECTTO, "connectto" },
   { SECCLASS_TCP_SOCKET, TCP_SOCKET__NEWCONN, "newconn" },
   { SECCLASS_TCP_SOCKET, TCP_SOCKET__ACCEPTFROM, "acceptfrom" },
   { SECCLASS_NODE, NODE__TCP_RECV, "tcp_recv" },
   { SECCLASS_NODE, NODE__TCP_SEND, "tcp_send" },
   { SECCLASS_NODE, NODE__UDP_RECV, "udp_recv" },
   { SECCLASS_NODE, NODE__UDP_SEND, "udp_send" },
   { SECCLASS_NODE, NODE__RAWIP_RECV, "rawip_recv" },
   { SECCLASS_NODE, NODE__RAWIP_SEND, "rawip_send" },
   { SECCLASS_NODE, NODE__ENFORCE_DEST, "enforce_dest" },
   { SECCLASS_NETIF, NETIF__TCP_RECV, "tcp_recv" },
   { SECCLASS_NETIF, NETIF__TCP_SEND, "tcp_send" },
   { SECCLASS_NETIF, NETIF__UDP_RECV, "udp_recv" },
   { SECCLASS_NETIF, NETIF__UDP_SEND, "udp_send" },
   { SECCLASS_NETIF, NETIF__RAWIP_RECV, "rawip_recv" },
   { SECCLASS_NETIF, NETIF__RAWIP_SEND, "rawip_send" },
   { SECCLASS_UNIX_STREAM_SOCKET, UNIX_STREAM_SOCKET__CONNECTTO, "connectto" },
   { SECCLASS_UNIX_STREAM_SOCKET, UNIX_STREAM_SOCKET__NEWCONN, "newconn" },
   { SECCLASS_UNIX_STREAM_SOCKET, UNIX_STREAM_SOCKET__ACCEPTFROM, "acceptfrom" },
   { SECCLASS_PROCESS, PROCESS__FORK, "fork" },
   { SECCLASS_PROCESS, PROCESS__TRANSITION, "transition" },
   { SECCLASS_PROCESS, PROCESS__SIGCHLD, "sigchld" },
   { SECCLASS_PROCESS, PROCESS__SIGKILL, "sigkill" },
   { SECCLASS_PROCESS, PROCESS__SIGSTOP, "sigstop" },
   { SECCLASS_PROCESS, PROCESS__SIGNULL, "signull" },
   { SECCLASS_PROCESS, PROCESS__SIGNAL, "signal" },
   { SECCLASS_PROCESS, PROCESS__PTRACE, "ptrace" },
   { SECCLASS_PROCESS, PROCESS__GETSCHED, "getsched" },
   { SECCLASS_PROCESS, PROCESS__SETSCHED, "setsched" },
   { SECCLASS_PROCESS, PROCESS__GETSESSION, "getsession" },
   { SECCLASS_PROCESS, PROCESS__GETPGID, "getpgid" },
   { SECCLASS_PROCESS, PROCESS__SETPGID, "setpgid" },
   { SECCLASS_PROCESS, PROCESS__GETCAP, "getcap" },
   { SECCLASS_PROCESS, PROCESS__SETCAP, "setcap" },
   { SECCLASS_PROCESS, PROCESS__SHARE, "share" },
   { SECCLASS_PROCESS, PROCESS__GETATTR, "getattr" },
   { SECCLASS_PROCESS, PROCESS__SETEXEC, "setexec" },
   { SECCLASS_PROCESS, PROCESS__SETFSCREATE, "setfscreate" },
   { SECCLASS_PROCESS, PROCESS__NOATSECURE, "noatsecure" },
   { SECCLASS_PROCESS, PROCESS__SIGINH, "siginh" },
   { SECCLASS_PROCESS, PROCESS__SETRLIMIT, "setrlimit" },
   { SECCLASS_PROCESS, PROCESS__RLIMITINH, "rlimitinh" },
   { SECCLASS_MSGQ, MSGQ__ENQUEUE, "enqueue" },
   { SECCLASS_MSG, MSG__SEND, "send" },
   { SECCLASS_MSG, MSG__RECEIVE, "receive" },
   { SECCLASS_SHM, SHM__LOCK, "lock" },
   { SECCLASS_SECURITY, SECURITY__COMPUTE_AV, "compute_av" },
   { SECCLASS_SECURITY, SECURITY__COMPUTE_CREATE, "compute_create" },
   { SECCLASS_SECURITY, SECURITY__COMPUTE_MEMBER, "compute_member" },
   { SECCLASS_SECURITY, SECURITY__CHECK_CONTEXT, "check_context" },
   { SECCLASS_SECURITY, SECURITY__LOAD_POLICY, "load_policy" },
   { SECCLASS_SECURITY, SECURITY__COMPUTE_RELABEL, "compute_relabel" },
   { SECCLASS_SECURITY, SECURITY__COMPUTE_USER, "compute_user" },
   { SECCLASS_SECURITY, SECURITY__SETENFORCE, "setenforce" },
   { SECCLASS_SYSTEM, SYSTEM__IPC_INFO, "ipc_info" },
   { SECCLASS_SYSTEM, SYSTEM__SYSLOG_READ, "syslog_read" },
   { SECCLASS_SYSTEM, SYSTEM__SYSLOG_MOD, "syslog_mod" },
   { SECCLASS_SYSTEM, SYSTEM__SYSLOG_CONSOLE, "syslog_console" },
   { SECCLASS_CAPABILITY, CAPABILITY__CHOWN, "chown" },
   { SECCLASS_CAPABILITY, CAPABILITY__DAC_OVERRIDE, "dac_override" },
   { SECCLASS_CAPABILITY, CAPABILITY__DAC_READ_SEARCH, "dac_read_search" },
   { SECCLASS_CAPABILITY, CAPABILITY__FOWNER, "fowner" },
   { SECCLASS_CAPABILITY, CAPABILITY__FSETID, "fsetid" },
   { SECCLASS_CAPABILITY, CAPABILITY__KILL, "kill" },
   { SECCLASS_CAPABILITY, CAPABILITY__SETGID, "setgid" },
   { SECCLASS_CAPABILITY, CAPABILITY__SETUID, "setuid" },
   { SECCLASS_CAPABILITY, CAPABILITY__SETPCAP, "setpcap" },
   { SECCLASS_CAPABILITY, CAPABILITY__LINUX_IMMUTABLE, "linux_immutable" },
   { SECCLASS_CAPABILITY, CAPABILITY__NET_BIND_SERVICE, "net_bind_service" },
   { SECCLASS_CAPABILITY, CAPABILITY__NET_BROADCAST, "net_broadcast" },
   { SECCLASS_CAPABILITY, CAPABILITY__NET_ADMIN, "net_admin" },
   { SECCLASS_CAPABILITY, CAPABILITY__NET_RAW, "net_raw" },
   { SECCLASS_CAPABILITY, CAPABILITY__IPC_LOCK, "ipc_lock" },
   { SECCLASS_CAPABILITY, CAPABILITY__IPC_OWNER, "ipc_owner" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_MODULE, "sys_module" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_RAWIO, "sys_rawio" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_CHROOT, "sys_chroot" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_PTRACE, "sys_ptrace" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_PACCT, "sys_pacct" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_ADMIN, "sys_admin" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_BOOT, "sys_boot" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_NICE, "sys_nice" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_RESOURCE, "sys_resource" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_TIME, "sys_time" },
   { SECCLASS_CAPABILITY, CAPABILITY__SYS_TTY_CONFIG, "sys_tty_config" },
   { SECCLASS_CAPABILITY, CAPABILITY__MKNOD, "mknod" },
   { SECCLASS_CAPABILITY, CAPABILITY__LEASE, "lease" },
   { SECCLASS_PASSWD, PASSWD__PASSWD, "passwd" },
   { SECCLASS_PASSWD, PASSWD__CHFN, "chfn" },
   { SECCLASS_PASSWD, PASSWD__CHSH, "chsh" },
};


/* FLASK */
