#ifndef AMIGAFFS_H
#define AMIGAFFS_H

#include <asm/byteorder.h>
#include <linux/types.h>

/* Ugly macros make the code more pretty. */

#define GET_END_PTR(st,p,sz)		 ((st *)((char *)(p)+((sz)-sizeof(st))))
#define AFFS_GET_HASHENTRY(data,hashkey) htonl(((struct dir_front *)data)->hashtable[hashkey])
#define AFFS_BLOCK(data,ino,blk)	 ((struct file_front *)data)->blocks[AFFS_I2HSIZE(ino)-1-blk]

#define FILE_END(p,i)	GET_END_PTR(struct file_end,p,AFFS_I2BSIZE(i))
#define ROOT_END(p,i)	GET_END_PTR(struct root_end,p,AFFS_I2BSIZE(i))
#define DIR_END(p,i)	GET_END_PTR(struct dir_end,p,AFFS_I2BSIZE(i))
#define LINK_END(p,i)	GET_END_PTR(struct hlink_end,p,AFFS_I2BSIZE(i))
#define ROOT_END_S(p,s)	GET_END_PTR(struct root_end,p,(s)->s_blocksize)
#define DATA_FRONT(bh)	((struct data_front *)(bh)->b_data)

/* Only for easier debugging if need be */
#define affs_bread	bread
#define affs_brelse	brelse

#ifdef __LITTLE_ENDIAN
#define BO_EXBITS	0x18UL
#elif defined(__BIG_ENDIAN)
#define BO_EXBITS	0x00UL
#else
#error Endianness must be known for affs to work.
#endif

/* The following constants will be checked against the values read native */

#define FS_OFS		0x444F5300
#define FS_FFS		0x444F5301
#define FS_INTLOFS	0x444F5302
#define FS_INTLFFS	0x444F5303
#define FS_DCOFS	0x444F5304
#define FS_DCFFS	0x444F5305
#define MUFS_FS		0x6d754653   /* 'muFS' */
#define MUFS_OFS	0x6d754600   /* 'muF\0' */
#define MUFS_FFS	0x6d754601   /* 'muF\1' */
#define MUFS_INTLOFS	0x6d754602   /* 'muF\2' */
#define MUFS_INTLFFS	0x6d754603   /* 'muF\3' */
#define MUFS_DCOFS	0x6d754604   /* 'muF\4' */
#define MUFS_DCFFS	0x6d754605   /* 'muF\5' */

struct DateStamp
{
  __u32 ds_Days;
  __u32 ds_Minute;
  __u32 ds_Tick;
};

#define T_SHORT		2
#define T_LIST		16
#define T_DATA		8

#define ST_LINKFILE	-4
#define ST_FILE		-3
#define ST_ROOT		1
#define ST_USERDIR	2
#define ST_SOFTLINK	3
#define ST_LINKDIR	4

struct root_front
{
  __s32 primary_type;
  __u32 spare1[2];
  __u32 hash_size;
  __u32 spare2;
  __u32 checksum;
  __u32 hashtable[0];
};

struct root_end
{
  __s32 bm_flag;
  __u32 bm_keys[25];
  __u32 bm_extend;
  struct DateStamp dir_altered;
  __u8 disk_name[40];
  struct DateStamp disk_altered;
  struct DateStamp disk_made;
  __u32 spare1[3];
  __s32 secondary_type;
};

struct dir_front
{
  __s32 primary_type;
  __u32 own_key;
  __u32 spare1[3];
  __u32 checksum;
  __u32 hashtable[0];
};

struct dir_end
{
  __u32 spare1;
  __u16 owner_uid;
  __u16 owner_gid;
  __u32 protect;
  __u32 spare2;
  __u8 comment[92];
  struct DateStamp created;
  __u8 dir_name[32];
  __u32 spare3[2];
  __u32 link_chain;
  __u32 spare4[5];
  __u32 hash_chain;
  __u32 parent;
  __u32 spare5;
  __s32 secondary_type;
};

struct file_front
{
  __s32 primary_type;
  __u32 own_key;
  __u32 block_count;
  __u32 unknown1;
  __u32 first_data;
  __u32 checksum;
  __u32 blocks[0];
};

struct file_end
{
  __u32 spare1;
  __u16 owner_uid;
  __u16 owner_gid;
  __u32 protect;
  __u32 byte_size;
  __u8 comment[92];
  struct DateStamp created;
  __u8 file_name[32];
  __u32 spare2;
  __u32 original;	/* not really in file_end */
  __u32 link_chain;
  __u32 spare3[5];
  __u32 hash_chain;
  __u32 parent;
  __u32 extension;
  __s32 secondary_type;
};

struct hlink_front
{
  __s32 primary_type;
  __u32 own_key;
  __u32 spare1[3];
  __u32 checksum;
};

struct hlink_end
{
  __u32 spare1;
  __u16 owner_uid;
  __u16 owner_gid;
  __u32 protect;
  __u8 comment[92];
  struct DateStamp created;
  __u8 link_name[32];
  __u32 spare2;
  __u32 original;
  __u32 link_chain;
  __u32 spare3[5];
  __u32 hash_chain;
  __u32 parent;
  __u32 spare4;
  __s32 secondary_type;
};

struct slink_front
{
  __s32 primary_type;
  __u32 own_key;
  __u32 spare1[3];
  __u32 checksum;
  __u8	symname[288];	/* depends on block size */
};

/* Permission bits */

#define FIBF_OTR_READ		0x8000
#define FIBF_OTR_WRITE		0x4000
#define FIBF_OTR_EXECUTE	0x2000
#define FIBF_OTR_DELETE		0x1000
#define FIBF_GRP_READ		0x0800
#define FIBF_GRP_WRITE		0x0400
#define FIBF_GRP_EXECUTE	0x0200
#define FIBF_GRP_DELETE		0x0100

#define FIBF_SCRIPT		0x0040
#define FIBF_PURE		0x0020		/* no use under linux */
#define FIBF_ARCHIVE		0x0010		/* never set, always cleared on write */
#define FIBF_READ		0x0008		/* 0 means allowed */
#define FIBF_WRITE		0x0004		/* 0 means allowed */
#define FIBF_EXECUTE		0x0002		/* 0 means allowed, ignored under linux */
#define FIBF_DELETE		0x0001		/* 0 means allowed */

#define FIBF_OWNER		0x000F		/* Bits pertaining to owner */

#define AFFS_UMAYWRITE(prot)	(((prot) & (FIBF_WRITE|FIBF_DELETE)) == (FIBF_WRITE|FIBF_DELETE))
#define AFFS_UMAYREAD(prot)	((prot) & FIBF_READ)
#define AFFS_UMAYEXECUTE(prot)	(((prot) & (FIBF_SCRIPT|FIBF_READ)) == (FIBF_SCRIPT|FIBF_READ))
#define AFFS_GMAYWRITE(prot)	(((prot)&(FIBF_GRP_WRITE|FIBF_GRP_DELETE))==\
							(FIBF_GRP_WRITE|FIBF_GRP_DELETE))
#define AFFS_GMAYREAD(prot)	((prot) & FIBF_GRP_READ)
#define AFFS_GMAYEXECUTE(prot)	(((prot)&(FIBF_SCRIPT|FIBF_GRP_READ))==(FIBF_SCRIPT|FIBF_GRP_READ))
#define AFFS_OMAYWRITE(prot)	(((prot)&(FIBF_OTR_WRITE|FIBF_OTR_DELETE))==\
							(FIBF_OTR_WRITE|FIBF_OTR_DELETE))
#define AFFS_OMAYREAD(prot)	((prot) & FIBF_OTR_READ)
#define AFFS_OMAYEXECUTE(prot)	(((prot)&(FIBF_SCRIPT|FIBF_OTR_READ))==(FIBF_SCRIPT|FIBF_OTR_READ))

#endif
