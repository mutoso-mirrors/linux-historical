
#ifndef _ISOFS_FS_H
#define _ISOFS_FS_H

#include <linux/types.h>
/*
 * The isofs filesystem constants/structures
 */

/* This part borrowed from the bsd386 isofs */
#define ISODCL(from, to) (to - from + 1)

struct iso_volume_descriptor {
	char type[ISODCL(1,1)]; /* 711 */
	char id[ISODCL(2,6)];
	char version[ISODCL(7,7)];
	char data[ISODCL(8,2048)];
};

/* volume descriptor types */
#define ISO_VD_PRIMARY 1
#define ISO_VD_END 255

#define ISO_STANDARD_ID "CD001"

struct iso_primary_descriptor {
	char type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	char version			[ISODCL (  7,   7)]; /* 711 */
	char unused1			[ISODCL (  8,   8)];
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	char unused2			[ISODCL ( 73,  80)];
	char volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	char unused3			[ISODCL ( 89, 120)];
	char volume_set_size		[ISODCL (121, 124)]; /* 723 */
	char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	char logical_block_size		[ISODCL (129, 132)]; /* 723 */
	char path_table_size		[ISODCL (133, 140)]; /* 733 */
	char type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	char type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	char modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	char file_structure_version	[ISODCL (882, 882)]; /* 711 */
	char unused4			[ISODCL (883, 883)];
	char application_data		[ISODCL (884, 1395)];
	char unused5			[ISODCL (1396, 2048)];
};


#define HS_STANDARD_ID "CDROM"

struct  hs_volume_descriptor {
	char foo			[ISODCL (  1,   8)]; /* 733 */
	char type			[ISODCL (  9,   9)]; /* 711 */
	char id				[ISODCL ( 10,  14)];
	char version			[ISODCL ( 15,  15)]; /* 711 */
	char data[ISODCL(16,2048)];
};


struct hs_primary_descriptor {
	char foo			[ISODCL (  1,   8)]; /* 733 */
	char type			[ISODCL (  9,   9)]; /* 711 */
	char id				[ISODCL ( 10,  14)];
	char version			[ISODCL ( 15,  15)]; /* 711 */
	char unused1			[ISODCL ( 16,  16)]; /* 711 */
	char system_id			[ISODCL ( 17,  48)]; /* achars */
	char volume_id			[ISODCL ( 49,  80)]; /* dchars */
	char unused2			[ISODCL ( 81,  88)]; /* 733 */
	char volume_space_size		[ISODCL ( 89,  96)]; /* 733 */
	char unused3			[ISODCL ( 97, 128)]; /* 733 */
	char volume_set_size		[ISODCL (129, 132)]; /* 723 */
	char volume_sequence_number	[ISODCL (133, 136)]; /* 723 */
	char logical_block_size		[ISODCL (137, 140)]; /* 723 */
	char path_table_size		[ISODCL (141, 148)]; /* 733 */
	char type_l_path_table		[ISODCL (149, 152)]; /* 731 */
	char unused4			[ISODCL (153, 180)]; /* 733 */
	char root_directory_record	[ISODCL (181, 214)]; /* 9.1 */
};

/* We use this to help us look up the parent inode numbers. */

struct iso_path_table{
	unsigned char  name_len[2];	/* 721 */
	char extent[4];		/* 731 */
	char  parent[2];	/* 721 */
	char name[0];
};

/* high sierra is identical to iso, except that the date is only 6 bytes, and
   there is an extra reserved byte after the flags */

struct iso_directory_record {
	char length			[ISODCL (1, 1)]; /* 711 */
	char ext_attr_length		[ISODCL (2, 2)]; /* 711 */
	char extent			[ISODCL (3, 10)]; /* 733 */
	char size			[ISODCL (11, 18)]; /* 733 */
	char date			[ISODCL (19, 25)]; /* 7 by 711 */
	char flags			[ISODCL (26, 26)];
	char file_unit_size		[ISODCL (27, 27)]; /* 711 */
	char interleave			[ISODCL (28, 28)]; /* 711 */
	char volume_sequence_number	[ISODCL (29, 32)]; /* 723 */
	unsigned char name_len		[ISODCL (33, 33)]; /* 711 */
	char name			[0];
};

#define ISOFS_BLOCK_BITS 11
#define ISOFS_BLOCK_SIZE 2048

#define ISOFS_BUFFER_SIZE(INODE) ((INODE)->i_sb->s_blocksize)
#define ISOFS_BUFFER_BITS(INODE) ((INODE)->i_sb->s_blocksize_bits)

#if 0
#ifdef ISOFS_FIXED_BLOCKSIZE
/* We use these until the buffer cache supports 2048 */
#define ISOFS_BUFFER_BITS 10
#define ISOFS_BUFFER_SIZE 1024

#define ISOFS_BLOCK_NUMBER(X) (X<<1)
#else
#define ISOFS_BUFFER_BITS 11
#define ISOFS_BUFFER_SIZE 2048

#define ISOFS_BLOCK_NUMBER(X) (X)
#endif
#endif

#define ISOFS_SUPER_MAGIC 0x9660
#define ISOFS_FILE_UNKNOWN 0
#define ISOFS_FILE_TEXT 1
#define ISOFS_FILE_BINARY 2
#define ISOFS_FILE_TEXT_M 3

#ifdef __KERNEL__
extern int isonum_711(char *);
extern int isonum_712(char *);
extern int isonum_721(char *);
extern int isonum_722(char *);
extern int isonum_723(char *);
extern int isonum_731(char *);
extern int isonum_732(char *);
extern int isonum_733(char *);
extern int iso_date(char *, int);

extern int parse_rock_ridge_inode(struct iso_directory_record *, struct inode *);
extern int get_rock_ridge_filename(struct iso_directory_record *, char ** name, int * len, struct inode *);

extern char * get_rock_ridge_symlink(struct inode *);
extern int find_rock_ridge_relocation(struct iso_directory_record *, struct inode *);

/* The stuff that follows may be totally unneeded. I have not checked to see 
 which prototypes we are still using.  */

extern int isofs_open(struct inode * inode, struct file * filp);
extern void isofs_release(struct inode * inode, struct file * filp);
extern int isofs_lookup(struct inode * dir,const char * name, int len,
	struct inode ** result);
extern unsigned long isofs_count_free_inodes(struct super_block *sb);
extern int isofs_new_block(int dev);
extern int isofs_free_block(int dev, int block);
extern int isofs_bmap(struct inode *,int);

extern void isofs_put_super(struct super_block *);
extern struct super_block *isofs_read_super(struct super_block *,void *,int);
extern void isofs_read_inode(struct inode *);
extern void isofs_put_inode(struct inode *);
extern void isofs_statfs(struct super_block *, struct statfs *);

extern int isofs_lseek(struct inode *, struct file *, off_t, int);
extern int isofs_read(struct inode *, struct file *, char *, int);
extern int isofs_lookup_grandparent(struct inode *, int);

extern struct inode_operations isofs_file_inode_operations;
extern struct inode_operations isofs_dir_inode_operations;
extern struct inode_operations isofs_symlink_inode_operations;
extern struct inode_operations isofs_chrdev_inode_operations;
extern struct inode_operations isofs_blkdev_inode_operations;
extern struct inode_operations isofs_fifo_inode_operations;

/* The following macros are used to check for memory leaks. */
#ifdef LEAK_CHECK
#define free_s leak_check_free_s
#define malloc leak_check_malloc
#define bread leak_check_bread
#define brelse leak_check_brelse
extern void * leak_check_malloc(unsigned int size);
extern void leak_check_free_s(void * obj, int size);
extern struct buffer_head * leak_check_bread(int dev, int block, int size);
extern void leak_check_brelse(struct buffer_head * bh);
#endif /* LEAK_CHECK */

#endif /* __KERNEL__ */

#endif



