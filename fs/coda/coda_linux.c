/*
 * Inode operations for Coda filesystem
 * Original version: (C) 1996 P. Braam and M. Callahan
 * Rewritten for Linux 2.1. (C) 1997 Carnegie Mellon University
 * 
 * Carnegie Mellon encourages users to contribute improvements to
 * the Coda project. Contact Peter Braam (coda@cs.cmu.edu).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/string.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_psdev.h>
#include <linux/coda_cnode.h>
#include <linux/coda_cache.h>

/* initialize the debugging variables */
int coda_debug = 0;
int coda_print_entry = 0; 
int coda_access_cache = 1;

/* caller must allocate 36 byte string ! */
char * coda_f2s(ViceFid *f, char *s)
{
	if ( f ) {
		sprintf(s, "(%-#10lx,%-#10lx,%-#10lx)", 
			 f->Volume, f->Vnode, f->Unique);
	}
	return s;
}

int coda_iscontrol(const char *name, size_t length)
{
	if ((CFS_CONTROLLEN == length) && 
	    (strncmp(name, CFS_CONTROL, CFS_CONTROLLEN) == 0))
		return 1;
	return 0;
}

int coda_isroot(struct inode *i)
{
    if ( i->i_sb->s_root->d_inode == i ) {
	return 1;
    } else {
	return 0;
    }
}

	
void coda_load_creds(struct coda_cred *cred)
{
        cred->cr_uid = (vuid_t) current->uid;
        cred->cr_euid = (vuid_t) current->euid;
        cred->cr_suid = (vuid_t) current->suid;
        cred->cr_fsuid = (vuid_t) current->fsuid;

        cred->cr_gid = (vgid_t) current->gid;
        cred->cr_egid = (vgid_t) current->egid;
        cred->cr_sgid = (vgid_t) current->sgid;
        cred->cr_fsgid = (vgid_t) current->fsgid;
}

int coda_cred_ok(struct coda_cred *cred)
{
	return(current->fsuid == cred->cr_fsuid);
}

int coda_cred_eq(struct coda_cred *cred1, struct coda_cred *cred2)
{
	return (cred1->cr_fsuid == cred2->cr_fsuid);
}

unsigned short coda_flags_to_cflags(unsigned short flags)
{
	unsigned short coda_flags = 0;

	if ( flags & (O_RDONLY | O_RDWR) )
		coda_flags |= C_O_READ;

	if ( flags & (O_WRONLY | O_RDWR) )
		coda_flags |= C_O_WRITE;

	if ( flags & O_TRUNC ) 
		coda_flags |= C_O_TRUNC;

	return coda_flags;
}


/* utility functions below */
void coda_vattr_to_iattr(struct inode *inode, struct coda_vattr *attr)
{
        int inode_type;
        /* inode's i_dev, i_flags, i_ino are set by iget 
           XXX: is this all we need ??
           */
        switch (attr->va_type) {
        case C_VNON:
                inode_type  = 0;
                break;
        case C_VREG:
                inode_type = S_IFREG;
                break;
        case C_VDIR:
                inode_type = S_IFDIR;
                break;
        case C_VLNK:
                inode_type = S_IFLNK;
                break;
        default:
                inode_type = 0;
        }
	inode->i_mode |= inode_type;

	if (attr->va_mode != (u_short) -1)
	        inode->i_mode = attr->va_mode | inode_type;
        if (attr->va_uid != -1) 
	        inode->i_uid = (uid_t) attr->va_uid;
        if (attr->va_gid != -1)
	        inode->i_gid = (gid_t) attr->va_gid;
	if (attr->va_nlink != -1)
	        inode->i_nlink = attr->va_nlink;
	if (attr->va_size != -1)
	        inode->i_size = attr->va_size;
	/*  XXX This needs further study */
	/*
        inode->i_blksize = attr->va_blocksize;
	inode->i_blocks = attr->va_size/attr->va_blocksize 
	  + (attr->va_size % attr->va_blocksize ? 1 : 0); 
	  */
	if (attr->va_atime.tv_sec != -1) 
	        inode->i_atime = attr->va_atime.tv_sec;
	if (attr->va_mtime.tv_sec != -1)
	        inode->i_mtime = attr->va_mtime.tv_sec;
        if (attr->va_ctime.tv_sec != -1)
	        inode->i_ctime = attr->va_ctime.tv_sec;
}
/* 
 * BSD sets attributes that need not be modified to -1. 
 * Linux uses the valid field to indicate what should be
 * looked at.  The BSD type field needs to be deduced from linux 
 * mode.
 * So we have to do some translations here.
 */

void coda_iattr_to_vattr(struct iattr *iattr, struct coda_vattr *vattr)
{
        umode_t mode;
        unsigned int valid;

        /* clean out */        
        vattr->va_mode = (umode_t) -1;
        vattr->va_uid = (vuid_t) -1; 
        vattr->va_gid = (vgid_t) -1;
        vattr->va_size = (off_t) -1;
	vattr->va_atime.tv_sec = (time_t) -1;
        vattr->va_mtime.tv_sec  = (time_t) -1;
	vattr->va_ctime.tv_sec  = (time_t) -1;
	vattr->va_atime.tv_nsec =  (time_t) -1;
        vattr->va_mtime.tv_nsec = (time_t) -1;
	vattr->va_ctime.tv_nsec = (time_t) -1;
        vattr->va_type = C_VNON;
	vattr->va_fileid = (long)-1;
	vattr->va_gen = (long)-1;
	vattr->va_bytes = (long)-1;
	vattr->va_nlink = (short)-1;
	vattr->va_blocksize = (long)-1;
	vattr->va_rdev = (dev_t)-1;
        vattr->va_flags = 0;

        /* determine the type */
        mode = iattr->ia_mode;
                if ( S_ISDIR(mode) ) {
                vattr->va_type = C_VDIR; 
        } else if ( S_ISREG(mode) ) {
                vattr->va_type = C_VREG;
        } else if ( S_ISLNK(mode) ) {
                vattr->va_type = C_VLNK;
        } else {
                /* don't do others */
                vattr->va_type = C_VNON;
        }

        /* set those vattrs that need change */
        valid = iattr->ia_valid;
        if ( valid & ATTR_MODE ) {
                vattr->va_mode = iattr->ia_mode;
	}
        if ( valid & ATTR_UID ) {
                vattr->va_uid = (vuid_t) iattr->ia_uid;
	}
        if ( valid & ATTR_GID ) {
                vattr->va_gid = (vgid_t) iattr->ia_gid;
	}
        if ( valid & ATTR_SIZE ) {
                vattr->va_size = iattr->ia_size;
	}
        if ( valid & ATTR_ATIME ) {
                vattr->va_atime.tv_sec = iattr->ia_atime;
                vattr->va_atime.tv_nsec = 0;
	}
        if ( valid & ATTR_MTIME ) {
                vattr->va_mtime.tv_sec = iattr->ia_mtime;
                vattr->va_mtime.tv_nsec = 0;
	}
        if ( valid & ATTR_CTIME ) {
                vattr->va_ctime.tv_sec = iattr->ia_ctime;
                vattr->va_ctime.tv_nsec = 0;
	}
        
}
  

void print_vattr(struct coda_vattr *attr)
{
    char *typestr;

    switch (attr->va_type) {
    case C_VNON:
	typestr = "C_VNON";
	break;
    case C_VREG:
	typestr = "C_VREG";
	break;
    case C_VDIR:
	typestr = "C_VDIR";
	break;
    case C_VBLK:
	typestr = "C_VBLK";
	break;
    case C_VCHR:
	typestr = "C_VCHR";
	break;
    case C_VLNK:
	typestr = "C_VLNK";
	break;
    case C_VSOCK:
	typestr = "C_VSCK";
	break;
    case C_VFIFO:
	typestr = "C_VFFO";
	break;
    case C_VBAD:
	typestr = "C_VBAD";
	break;
    default:
	typestr = "????";
	break;
    }


    printk("attr: type %s (%o)  mode %o uid %d gid %d rdev %d\n",
	   typestr, (int)attr->va_type, (int)attr->va_mode, 
	   (int)attr->va_uid, (int)attr->va_gid, (int)attr->va_rdev);
    
    printk("      fileid %d nlink %d size %d blocksize %d bytes %d\n",
	      (int)attr->va_fileid, (int)attr->va_nlink, 
	      (int)attr->va_size,
	      (int)attr->va_blocksize,(int)attr->va_bytes);
    printk("      gen %ld flags %ld\n",
	      attr->va_gen, attr->va_flags);
    printk("      atime sec %d nsec %d\n",
	      (int)attr->va_atime.tv_sec, (int)attr->va_atime.tv_nsec);
    printk("      mtime sec %d nsec %d\n",
	      (int)attr->va_mtime.tv_sec, (int)attr->va_mtime.tv_nsec);
    printk("      ctime sec %d nsec %d\n",
	      (int)attr->va_ctime.tv_sec, (int)attr->va_ctime.tv_nsec);
}
