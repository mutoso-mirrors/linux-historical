/*
 *  linux/fs/fat/dir.c
 *
 *  directory handling functions for fat-based filesystems
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 *
 *  VFAT extensions by Gordon Chaffee <chaffee@plateau.cs.berkeley.edu>
 *  Merged with msdos fs by Henrik Storner <storner@osiris.ping.dk>
 *  Rewritten for constant inumbers. Plugged buffer overrun in readdir(). AV
 *  Short name translation 1999, 2001 by Wolfram Pienkoss <wp@bszh.de>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/msdos_fs.h>
#include <linux/dirent.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>

/*
 * Convert Unicode 16 to UTF8, translated Unicode, or ASCII.
 * If uni_xlate is enabled and we can't get a 1:1 conversion, use a
 * colon as an escape character since it is normally invalid on the vfat
 * filesystem. The following four characters are the hexadecimal digits
 * of Unicode value. This lets us do a full dump and restore of Unicode
 * filenames. We could get into some trouble with long Unicode names,
 * but ignore that right now.
 * Ahem... Stack smashing in ring 0 isn't fun. Fixed.
 */
static int uni16_to_x8(unsigned char *ascii, wchar_t *uni, int uni_xlate,
		       struct nls_table *nls)
{
	wchar_t *ip, ec;
	unsigned char *op, nc;
	int charlen;
	int k;

	ip = uni;
	op = ascii;

	while (*ip) {
		ec = *ip++;
		if ( (charlen = nls->uni2char(ec, op, NLS_MAX_CHARSET_SIZE)) > 0) {
			op += charlen;
		} else {
			if (uni_xlate == 1) {
				*op = ':';
				for (k = 4; k > 0; k--) {
					nc = ec & 0xF;
					op[k] = nc > 9	? nc + ('a' - 10)
							: nc + '0';
					ec >>= 4;
				}
				op += 5;
			} else {
				*op++ = '?';
			}
		}
		/* We have some slack there, so it's OK */
		if (op>ascii+256) {
			op = ascii + 256;
			break;
		}
	}
	*op = 0;
	return (op - ascii);
}

static inline int
fat_short2uni(struct nls_table *t, unsigned char *c, int clen, wchar_t *uni)
{
	int charlen;

	charlen = t->char2uni(c, clen, uni);
	if (charlen < 0) {
		*uni = 0x003f;	/* a question mark */
		charlen = 1;
	}
	return charlen;
}

static inline int
fat_short2lower_uni(struct nls_table *t, unsigned char *c, int clen, wchar_t *uni)
{
	int charlen;
	wchar_t wc;

	charlen = t->char2uni(c, clen, &wc);
	if (charlen < 0) {
		*uni = 0x003f;	/* a question mark */
		charlen = 1;
	} else if (charlen <= 1) {
		unsigned char nc = t->charset2lower[*c];

		if (!nc)
			nc = *c;

		if ( (charlen = t->char2uni(&nc, 1, uni)) < 0) {
			*uni = 0x003f;	/* a question mark */
			charlen = 1;
		}
	} else
		*uni = wc;

	return charlen;
}

static inline int
fat_shortname2uni(struct nls_table *nls, unsigned char *buf, int buf_size,
		  wchar_t *uni_buf, unsigned short opt, int lower)
{
	int len = 0;

	if (opt & VFAT_SFN_DISPLAY_LOWER)
		len =  fat_short2lower_uni(nls, buf, buf_size, uni_buf);
	else if (opt & VFAT_SFN_DISPLAY_WIN95)
		len = fat_short2uni(nls, buf, buf_size, uni_buf);
	else if (opt & VFAT_SFN_DISPLAY_WINNT) {
		if (lower)
			len = fat_short2lower_uni(nls, buf, buf_size, uni_buf);
		else
			len = fat_short2uni(nls, buf, buf_size, uni_buf);
	} else
		len = fat_short2uni(nls, buf, buf_size, uni_buf);

	return len;
}

/*
 * Return values: negative -> error, 0 -> not found, positive -> found,
 * value is the total amount of slots, including the shortname entry.
 */
int fat_search_long(struct inode *inode, const unsigned char *name,
		    int name_len, struct fat_slot_info *sinfo)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	struct nls_table *nls_io = MSDOS_SB(sb)->nls_io;
	struct nls_table *nls_disk = MSDOS_SB(sb)->nls_disk;
	wchar_t bufuname[14];
	unsigned char xlate_len, nr_slots;
	wchar_t *unicode = NULL;
	unsigned char work[8], bufname[260];	/* 256 + 4 */
	int uni_xlate = MSDOS_SB(sb)->options.unicode_xlate;
	int utf8 = MSDOS_SB(sb)->options.utf8;
	int anycase = (MSDOS_SB(sb)->options.name_check != 's');
	unsigned short opt_shortname = MSDOS_SB(sb)->options.shortname;
	int chl, i, j, last_u, err;
	loff_t i_pos, cpos = 0;

	err = -ENOENT;
	while(1) {
		if (fat_get_entry(inode, &cpos, &bh, &de, &i_pos) == -1)
			goto EODir;
parse_record:
		nr_slots = 0;
		if (de->name[0] == DELETED_FLAG)
			continue;
		if (de->attr != ATTR_EXT && (de->attr & ATTR_VOLUME))
			continue;
		if (de->attr != ATTR_EXT && IS_FREE(de->name))
			continue;
		if (de->attr == ATTR_EXT) {
			struct msdos_dir_slot *ds;
			unsigned char id;
			unsigned char slot;
			unsigned char slots;
			unsigned char sum;
			unsigned char alias_checksum;

			if (!unicode) {
				unicode = (wchar_t *)
					__get_free_page(GFP_KERNEL);
				if (!unicode) {
					brelse(bh);
					return -ENOMEM;
				}
			}
parse_long:
			slots = 0;
			ds = (struct msdos_dir_slot *) de;
			id = ds->id;
			if (!(id & 0x40))
				continue;
			slots = id & ~0x40;
			if (slots > 20 || !slots)	/* ceil(256 * 2 / 26) */
				continue;
			nr_slots = slots;
			alias_checksum = ds->alias_checksum;

			slot = slots;
			while (1) {
				int offset;

				slot--;
				offset = slot * 13;
				fat16_towchar(unicode + offset, ds->name0_4, 5);
				fat16_towchar(unicode + offset + 5, ds->name5_10, 6);
				fat16_towchar(unicode + offset + 11, ds->name11_12, 2);

				if (ds->id & 0x40) {
					unicode[offset + 13] = 0;
				}
				if (fat_get_entry(inode,&cpos,&bh,&de,&i_pos)<0)
					goto EODir;
				if (slot == 0)
					break;
				ds = (struct msdos_dir_slot *) de;
				if (ds->attr !=  ATTR_EXT)
					goto parse_record;
				if ((ds->id & ~0x40) != slot)
					goto parse_long;
				if (ds->alias_checksum != alias_checksum)
					goto parse_long;
			}
			if (de->name[0] == DELETED_FLAG)
				continue;
			if (de->attr ==  ATTR_EXT)
				goto parse_long;
			if (IS_FREE(de->name) || (de->attr & ATTR_VOLUME))
				continue;
			for (sum = 0, i = 0; i < 11; i++)
				sum = (((sum&1)<<7)|((sum&0xfe)>>1)) + de->name[i];
			if (sum != alias_checksum)
				nr_slots = 0;
		}

		memcpy(work, de->name, sizeof(de->name));
		/* see namei.c, msdos_format_name */
		if (work[0] == 0x05)
			work[0] = 0xE5;
		for (i = 0, j = 0, last_u = 0; i < 8;) {
			if (!work[i]) break;
			chl = fat_shortname2uni(nls_disk, &work[i], 8 - i,
						&bufuname[j++], opt_shortname,
						de->lcase & CASE_LOWER_BASE);
			if (chl <= 1) {
				if (work[i] != ' ')
					last_u = j;
			} else {
				last_u = j;
			}
			i += chl;
		}
		j = last_u;
		fat_short2uni(nls_disk, ".", 1, &bufuname[j++]);
		for (i = 0; i < 3;) {
			if (!de->ext[i]) break;
			chl = fat_shortname2uni(nls_disk, &de->ext[i], 3 - i,
						&bufuname[j++], opt_shortname,
						de->lcase & CASE_LOWER_EXT);
			if (chl <= 1) {
				if (de->ext[i] != ' ')
					last_u = j;
			} else {
				last_u = j;
			}
			i += chl;
		}
		if (!last_u)
			continue;

		bufuname[last_u] = 0x0000;
		xlate_len = utf8
			?utf8_wcstombs(bufname, bufuname, sizeof(bufname))
			:uni16_to_x8(bufname, bufuname, uni_xlate, nls_io);
		if (xlate_len == name_len)
			if ((!anycase && !memcmp(name, bufname, xlate_len)) ||
			    (anycase && !nls_strnicmp(nls_io, name, bufname,
								xlate_len)))
				goto Found;

		if (nr_slots) {
			xlate_len = utf8
				?utf8_wcstombs(bufname, unicode, sizeof(bufname))
				:uni16_to_x8(bufname, unicode, uni_xlate, nls_io);
			if (xlate_len != name_len)
				continue;
			if ((!anycase && !memcmp(name, bufname, xlate_len)) ||
			    (anycase && !nls_strnicmp(nls_io, name, bufname,
								xlate_len)))
				goto Found;
		}
	}

Found:
	nr_slots++;	/* include the de */
	sinfo->i_pos = i_pos;
	sinfo->slot_off = cpos - nr_slots * sizeof(*de);
	sinfo->nr_slots = nr_slots;
	sinfo->de = de;
	sinfo->bh = bh;
	err = 0;
EODir:
	if (unicode)
		free_page((unsigned long)unicode);

	return err;
}

EXPORT_SYMBOL(fat_search_long);

struct fat_ioctl_filldir_callback {
	struct dirent __user *dirent;
	int result;
	/* for dir ioctl */
	const char *longname;
	int long_len;
	const char *shortname;
	int short_len;
};

static int fat_readdirx(struct inode *inode, struct file *filp, void *dirent,
			filldir_t filldir, int short_only, int both)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct nls_table *nls_io = MSDOS_SB(sb)->nls_io;
	struct nls_table *nls_disk = MSDOS_SB(sb)->nls_disk;
	unsigned char long_slots;
	const char *fill_name;
	int fill_len;
	wchar_t bufuname[14];
	wchar_t *unicode = NULL;
	unsigned char c, work[8], bufname[56], *ptname = bufname;
	unsigned long lpos, dummy, *furrfu = &lpos;
	int uni_xlate = MSDOS_SB(sb)->options.unicode_xlate;
	int isvfat = MSDOS_SB(sb)->options.isvfat;
	int utf8 = MSDOS_SB(sb)->options.utf8;
	int nocase = MSDOS_SB(sb)->options.nocase;
	unsigned short opt_shortname = MSDOS_SB(sb)->options.shortname;
	unsigned long inum;
	int chi, chl, i, i2, j, last, last_u, dotoffset = 0;
	loff_t i_pos, cpos;
	int ret = 0;

	lock_kernel();

	cpos = filp->f_pos;
	/* Fake . and .. for the root directory. */
	if (inode->i_ino == MSDOS_ROOT_INO) {
		while (cpos < 2) {
			if (filldir(dirent, "..", cpos+1, cpos, MSDOS_ROOT_INO, DT_DIR) < 0)
				goto out;
			cpos++;
			filp->f_pos++;
		}
		if (cpos == 2) {
			dummy = 2;
			furrfu = &dummy;
			cpos = 0;
		}
	}
	if (cpos & (sizeof(struct msdos_dir_entry)-1)) {
		ret = -ENOENT;
		goto out;
	}

	bh = NULL;
GetNew:
	long_slots = 0;
	if (fat_get_entry(inode,&cpos,&bh,&de,&i_pos) == -1)
		goto EODir;
	/* Check for long filename entry */
	if (isvfat) {
		if (de->name[0] == DELETED_FLAG)
			goto RecEnd;
		if (de->attr != ATTR_EXT && (de->attr & ATTR_VOLUME))
			goto RecEnd;
		if (de->attr != ATTR_EXT && IS_FREE(de->name))
			goto RecEnd;
	} else {
		if ((de->attr & ATTR_VOLUME) || IS_FREE(de->name))
			goto RecEnd;
	}

	if (isvfat && de->attr == ATTR_EXT) {
		struct msdos_dir_slot *ds;
		unsigned char id;
		unsigned char slot;
		unsigned char slots;
		unsigned char sum;
		unsigned char alias_checksum;

		if (!unicode) {
			unicode = (wchar_t *)__get_free_page(GFP_KERNEL);
			if (!unicode) {
				filp->f_pos = cpos;
				brelse(bh);
				ret = -ENOMEM;
				goto out;
			}
		}
ParseLong:
		slots = 0;
		ds = (struct msdos_dir_slot *) de;
		id = ds->id;
		if (!(id & 0x40))
			goto RecEnd;
		slots = id & ~0x40;
		if (slots > 20 || !slots)	/* ceil(256 * 2 / 26) */
			goto RecEnd;
		long_slots = slots;
		alias_checksum = ds->alias_checksum;

		slot = slots;
		while (1) {
			int offset;

			slot--;
			offset = slot * 13;
			fat16_towchar(unicode + offset, ds->name0_4, 5);
			fat16_towchar(unicode + offset + 5, ds->name5_10, 6);
			fat16_towchar(unicode + offset + 11, ds->name11_12, 2);

			if (ds->id & 0x40) {
				unicode[offset + 13] = 0;
			}
			if (fat_get_entry(inode,&cpos,&bh,&de,&i_pos) == -1)
				goto EODir;
			if (slot == 0)
				break;
			ds = (struct msdos_dir_slot *) de;
			if (ds->attr !=  ATTR_EXT)
				goto RecEnd;	/* XXX */
			if ((ds->id & ~0x40) != slot)
				goto ParseLong;
			if (ds->alias_checksum != alias_checksum)
				goto ParseLong;
		}
		if (de->name[0] == DELETED_FLAG)
			goto RecEnd;
		if (de->attr ==  ATTR_EXT)
			goto ParseLong;
		if (IS_FREE(de->name) || (de->attr & ATTR_VOLUME))
			goto RecEnd;
		for (sum = 0, i = 0; i < 11; i++)
			sum = (((sum&1)<<7)|((sum&0xfe)>>1)) + de->name[i];
		if (sum != alias_checksum)
			long_slots = 0;
	}

	if (MSDOS_SB(sb)->options.dotsOK) {
		ptname = bufname;
		dotoffset = 0;
		if (de->attr & ATTR_HIDDEN) {
			*ptname++ = '.';
			dotoffset = 1;
		}
	}

	memcpy(work, de->name, sizeof(de->name));
	/* see namei.c, msdos_format_name */
	if (work[0] == 0x05)
		work[0] = 0xE5;
	for (i = 0, j = 0, last = 0, last_u = 0; i < 8;) {
		if (!(c = work[i])) break;
		chl = fat_shortname2uni(nls_disk, &work[i], 8 - i,
					&bufuname[j++], opt_shortname,
					de->lcase & CASE_LOWER_BASE);
		if (chl <= 1) {
			ptname[i++] = (!nocase && c>='A' && c<='Z') ? c+32 : c;
			if (c != ' ') {
				last = i;
				last_u = j;
			}
		} else {
			last_u = j;
			for (chi = 0; chi < chl && i < 8; chi++) {
				ptname[i] = work[i];
				i++; last = i;
			}
		}
	}
	i = last;
	j = last_u;
	fat_short2uni(nls_disk, ".", 1, &bufuname[j++]);
	ptname[i++] = '.';
	for (i2 = 0; i2 < 3;) {
		if (!(c = de->ext[i2])) break;
		chl = fat_shortname2uni(nls_disk, &de->ext[i2], 3 - i2,
					&bufuname[j++], opt_shortname,
					de->lcase & CASE_LOWER_EXT);
		if (chl <= 1) {
			i2++;
			ptname[i++] = (!nocase && c>='A' && c<='Z') ? c+32 : c;
			if (c != ' ') {
				last = i;
				last_u = j;
			}
		} else {
			last_u = j;
			for (chi = 0; chi < chl && i2 < 3; chi++) {
				ptname[i++] = de->ext[i2++];
				last = i;
			}
		}
	}
	if (!last)
		goto RecEnd;

	i = last + dotoffset;
	j = last_u;

	lpos = cpos - (long_slots+1)*sizeof(struct msdos_dir_entry);
	if (!memcmp(de->name, MSDOS_DOT, MSDOS_NAME))
		inum = inode->i_ino;
	else if (!memcmp(de->name, MSDOS_DOTDOT, MSDOS_NAME)) {
		inum = parent_ino(filp->f_dentry);
	} else {
		struct inode *tmp = fat_iget(sb, i_pos);
		if (tmp) {
			inum = tmp->i_ino;
			iput(tmp);
		} else
			inum = iunique(sb, MSDOS_ROOT_INO);
	}

	if (isvfat) {
		bufuname[j] = 0x0000;
		i = utf8 ? utf8_wcstombs(bufname, bufuname, sizeof(bufname))
			 : uni16_to_x8(bufname, bufuname, uni_xlate, nls_io);
	}

	fill_name = bufname;
	fill_len = i;
	if (!short_only && long_slots) {
		/* convert the unicode long name. 261 is maximum size
		 * of unicode buffer. (13 * slots + nul) */
		void *longname = unicode + 261;
		int buf_size = PAGE_SIZE - (261 * sizeof(unicode[0]));
		int long_len = utf8
			? utf8_wcstombs(longname, unicode, buf_size)
			: uni16_to_x8(longname, unicode, uni_xlate, nls_io);

		if (!both) {
			fill_name = longname;
			fill_len = long_len;
		} else {
			/* hack for fat_ioctl_filldir() */
			struct fat_ioctl_filldir_callback *p = dirent;

			p->longname = longname;
			p->long_len = long_len;
			p->shortname = bufname;
			p->short_len = i;
			fill_name = NULL;
			fill_len = 0;
		}
	}
	if (filldir(dirent, fill_name, fill_len, *furrfu, inum,
		    (de->attr & ATTR_DIR) ? DT_DIR : DT_REG) < 0)
		goto FillFailed;

RecEnd:
	furrfu = &lpos;
	filp->f_pos = cpos;
	goto GetNew;
EODir:
	filp->f_pos = cpos;
FillFailed:
	if (bh)
		brelse(bh);
	if (unicode)
		free_page((unsigned long)unicode);
out:
	unlock_kernel();
	return ret;
}

static int fat_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	return fat_readdirx(inode, filp, dirent, filldir, 0, 0);
}

static int fat_ioctl_filldir(void *__buf, const char *name, int name_len,
			     loff_t offset, ino_t ino, unsigned int d_type)
{
	struct fat_ioctl_filldir_callback *buf = __buf;
	struct dirent __user *d1 = buf->dirent;
	struct dirent __user *d2 = d1 + 1;

	if (buf->result)
		return -EINVAL;
	buf->result++;

	if (name != NULL) {
		/* dirent has only short name */
		if (name_len >= sizeof(d1->d_name))
			name_len = sizeof(d1->d_name) - 1;

		if (put_user(0, d2->d_name)			||
		    put_user(0, &d2->d_reclen)			||
		    copy_to_user(d1->d_name, name, name_len)	||
		    put_user(0, d1->d_name + name_len)		||
		    put_user(name_len, &d1->d_reclen))
			goto efault;
	} else {
		/* dirent has short and long name */
		const char *longname = buf->longname;
		int long_len = buf->long_len;
		const char *shortname = buf->shortname;
		int short_len = buf->short_len;

		if (long_len >= sizeof(d1->d_name))
			long_len = sizeof(d1->d_name) - 1;
		if (short_len >= sizeof(d1->d_name))
			short_len = sizeof(d1->d_name) - 1;

		if (copy_to_user(d2->d_name, longname, long_len)	||
		    put_user(0, d2->d_name + long_len)			||
		    put_user(long_len, &d2->d_reclen)			||
		    put_user(ino, &d2->d_ino)				||
		    put_user(offset, &d2->d_off)			||
		    copy_to_user(d1->d_name, shortname, short_len)	||
		    put_user(0, d1->d_name + short_len)			||
		    put_user(short_len, &d1->d_reclen))
			goto efault;
	}
	return 0;
efault:
	buf->result = -EFAULT;
	return -EFAULT;
}

static int fat_dir_ioctl(struct inode * inode, struct file * filp,
		  unsigned int cmd, unsigned long arg)
{
	struct fat_ioctl_filldir_callback buf;
	struct dirent __user *d1;
	int ret, short_only, both;

	switch (cmd) {
	case VFAT_IOCTL_READDIR_SHORT:
		short_only = 1;
		both = 0;
		break;
	case VFAT_IOCTL_READDIR_BOTH:
		short_only = 0;
		both = 1;
		break;
	default:
		return fat_generic_ioctl(inode, filp, cmd, arg);
	}

	d1 = (struct dirent __user *)arg;
	if (!access_ok(VERIFY_WRITE, d1, sizeof(struct dirent[2])))
		return -EFAULT;
	/*
	 * Yes, we don't need this put_user() absolutely. However old
	 * code didn't return the right value. So, app use this value,
	 * in order to check whether it is EOF.
	 */
	if (put_user(0, &d1->d_reclen))
		return -EFAULT;

	buf.dirent = d1;
	buf.result = 0;
	down(&inode->i_sem);
	ret = -ENOENT;
	if (!IS_DEADDIR(inode)) {
		ret = fat_readdirx(inode, filp, &buf, fat_ioctl_filldir,
				   short_only, both);
	}
	up(&inode->i_sem);
	if (ret >= 0)
		ret = buf.result;
	return ret;
}

struct file_operations fat_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= fat_readdir,
	.ioctl		= fat_dir_ioctl,
	.fsync		= file_fsync,
};

static int fat_get_short_entry(struct inode *dir, loff_t *pos,
			       struct buffer_head **bh,
			       struct msdos_dir_entry **de, loff_t *i_pos)
{
	while (fat_get_entry(dir, pos, bh, de, i_pos) >= 0) {
		/* free entry or long name entry or volume label */
		if (!IS_FREE((*de)->name) && !((*de)->attr & ATTR_VOLUME))
			return 0;
	}
	return -ENOENT;
}

/*
 * The ".." entry can not provide the "struct fat_slot_info" informations
 * for inode. So, this function provide the some informations only.
 */
int fat_get_dotdot_entry(struct inode *dir, struct buffer_head **bh,
			 struct msdos_dir_entry **de, loff_t *i_pos)
{
	loff_t offset;

	offset = 0;
	*bh = NULL;
	while (fat_get_short_entry(dir, &offset, bh, de, i_pos) >= 0) {
		if (!strncmp((*de)->name, MSDOS_DOTDOT, MSDOS_NAME))
			return 0;
	}
	return -ENOENT;
}

EXPORT_SYMBOL(fat_get_dotdot_entry);

/* See if directory is empty */
int fat_dir_empty(struct inode *dir)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	loff_t cpos, i_pos;
	int result = 0;

	bh = NULL;
	cpos = 0;
	while (fat_get_short_entry(dir, &cpos, &bh, &de, &i_pos) >= 0) {
		if (strncmp(de->name, MSDOS_DOT   , MSDOS_NAME) &&
		    strncmp(de->name, MSDOS_DOTDOT, MSDOS_NAME)) {
			result = -ENOTEMPTY;
			break;
		}
	}
	brelse(bh);
	return result;
}

EXPORT_SYMBOL(fat_dir_empty);

/*
 * fat_subdirs counts the number of sub-directories of dir. It can be run
 * on directories being created.
 */
int fat_subdirs(struct inode *dir)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	loff_t cpos, i_pos;
	int count = 0;

	bh = NULL;
	cpos = 0;
	while (fat_get_short_entry(dir, &cpos, &bh, &de, &i_pos) >= 0) {
		if (de->attr & ATTR_DIR)
			count++;
	}
	brelse(bh);
	return count;
}

/*
 * Scans a directory for a given file (name points to its formatted name).
 * Returns an error code or zero.
 */
int fat_scan(struct inode *dir, const unsigned char *name,
	     struct fat_slot_info *sinfo)
{
	sinfo->slot_off = 0;
	sinfo->bh = NULL;
	while (fat_get_short_entry(dir, &sinfo->slot_off, &sinfo->bh,
				   &sinfo->de, &sinfo->i_pos) >= 0) {
		if (!strncmp(sinfo->de->name, name, MSDOS_NAME)) {
			sinfo->slot_off -= sizeof(*sinfo->de);
			sinfo->nr_slots = 1;
			return 0;
		}
	}
	return -ENOENT;
}

EXPORT_SYMBOL(fat_scan);

static int __fat_remove_entries(struct inode *dir, loff_t pos, int nr_slots)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de, *endp;
	loff_t i_pos;
	int err = 0, orig_slots;

	while (nr_slots) {
		bh = NULL;
		if (fat_get_entry(dir, &pos, &bh, &de, &i_pos) < 0) {
			err = -EIO;
			break;
		}

		orig_slots = nr_slots;
		endp = (struct msdos_dir_entry *)(bh->b_data + sb->s_blocksize);
		while (nr_slots && de < endp) {
			de->name[0] = DELETED_FLAG;
			de++;
			nr_slots--;
		}
		mark_buffer_dirty(bh);
		if (IS_DIRSYNC(dir))
			err = sync_dirty_buffer(bh);
		brelse(bh);
		if (err)
			break;

		/* pos is *next* de's position, so this does `- sizeof(de)' */
		pos += ((orig_slots - nr_slots) * sizeof(*de)) - sizeof(*de);
	}

	return err;
}

int fat_remove_entries(struct inode *dir, struct fat_slot_info *sinfo)
{
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
	int err = 0, nr_slots;

	/*
	 * First stage: Remove the shortname. By this, the directory
	 * entry is removed.
	 */
	nr_slots = sinfo->nr_slots;
	de = sinfo->de;
	sinfo->de = NULL;
	bh = sinfo->bh;
	sinfo->bh = NULL;
	while (nr_slots && de >= (struct msdos_dir_entry *)bh->b_data) {
		de->name[0] = DELETED_FLAG;
		de--;
		nr_slots--;
	}
	mark_buffer_dirty(bh);
	if (IS_DIRSYNC(dir))
		err = sync_dirty_buffer(bh);
	brelse(bh);
	if (err)
		return err;
	dir->i_version++;

	if (nr_slots) {
		/*
		 * Second stage: remove the remaining longname slots.
		 * (This directory entry is already removed, and so return
		 * the success)
		 */
		err = __fat_remove_entries(dir, sinfo->slot_off, nr_slots);
		if (err) {
			printk(KERN_WARNING
			       "FAT: Couldn't remove the long name slots\n");
		}
	}

	dir->i_mtime = dir->i_atime = CURRENT_TIME_SEC;
	if (IS_DIRSYNC(dir))
		(void)fat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	return 0;
}

EXPORT_SYMBOL(fat_remove_entries);

static int fat_zeroed_cluster(struct inode *dir, sector_t blknr, int nr_used,
			      struct buffer_head **bhs, int nr_bhs)
{
	struct super_block *sb = dir->i_sb;
	sector_t last_blknr = blknr + MSDOS_SB(sb)->sec_per_clus;
	int err, i, n;

	/* Zeroing the unused blocks on this cluster */
	blknr += nr_used;
	n = nr_used;
	while (blknr < last_blknr) {
		bhs[n] = sb_getblk(sb, blknr);
		if (!bhs[n]) {
			err = -ENOMEM;
			goto error;
		}
		memset(bhs[n]->b_data, 0, sb->s_blocksize);
		set_buffer_uptodate(bhs[n]);
		mark_buffer_dirty(bhs[n]);

		n++;
		blknr++;
		if (n == nr_bhs) {
			if (IS_DIRSYNC(dir)) {
				err = fat_sync_bhs(bhs, n);
				if (err)
					goto error;
			}
			for (i = 0; i < n; i++)
				brelse(bhs[i]);
			n = 0;
		}
	}
	if (IS_DIRSYNC(dir)) {
		err = fat_sync_bhs(bhs, n);
		if (err)
			goto error;
	}
	for (i = 0; i < n; i++)
		brelse(bhs[i]);

	return 0;

error:
	for (i = 0; i < n; i++)
		bforget(bhs[i]);
	return err;
}

int fat_alloc_new_dir(struct inode *dir, struct timespec *ts)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	struct msdos_dir_entry *de;
	sector_t blknr;
	__le16 date, time;
	int err, cluster;

	err = fat_alloc_clusters(dir, &cluster, 1);
	if (err)
		goto error;

	blknr = fat_clus_to_blknr(sbi, cluster);
	bhs[0] = sb_getblk(sb, blknr);
	if (!bhs[0]) {
		err = -ENOMEM;
		goto error_free;
	}

	fat_date_unix2dos(ts->tv_sec, &time, &date);

	de = (struct msdos_dir_entry *)bhs[0]->b_data;
	/* filling the new directory slots ("." and ".." entries) */
	memcpy(de[0].name, MSDOS_DOT, MSDOS_NAME);
	memcpy(de[1].name, MSDOS_DOTDOT, MSDOS_NAME);
	de->attr = de[1].attr = ATTR_DIR;
	de[0].lcase = de[1].lcase = 0;
	de[0].time = de[1].time = time;
	de[0].date = de[1].date = date;
	de[0].ctime_cs = de[1].ctime_cs = 0;
	if (sbi->options.isvfat) {
		/* extra timestamps */
		de[0].ctime = de[1].ctime = time;
		de[0].adate = de[0].cdate = de[1].adate = de[1].cdate = date;
	} else {
		de[0].ctime = de[1].ctime = 0;
		de[0].adate = de[0].cdate = de[1].adate = de[1].cdate = 0;
	}
	de[0].start = cpu_to_le16(cluster);
	de[0].starthi = cpu_to_le16(cluster >> 16);
	de[1].start = cpu_to_le16(MSDOS_I(dir)->i_logstart);
	de[1].starthi = cpu_to_le16(MSDOS_I(dir)->i_logstart >> 16);
	de[0].size = de[1].size = 0;
	memset(de + 2, 0, sb->s_blocksize - 2 * sizeof(*de));
	set_buffer_uptodate(bhs[0]);
	mark_buffer_dirty(bhs[0]);

	err = fat_zeroed_cluster(dir, blknr, 1, bhs, MAX_BUF_PER_PAGE);
	if (err)
		goto error_free;

	return cluster;

error_free:
	fat_free_clusters(dir, cluster);
error:
	return err;
}

EXPORT_SYMBOL(fat_alloc_new_dir);

static struct buffer_head *fat_extend_dir(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh, *res = NULL;
	int err, cluster, sec_per_clus = MSDOS_SB(sb)->sec_per_clus;
	sector_t sector, last_sector;

	if (MSDOS_SB(sb)->fat_bits != 32) {
		if (inode->i_ino == MSDOS_ROOT_INO)
			return ERR_PTR(-ENOSPC);
	}

	err = fat_alloc_clusters(inode, &cluster, 1);
	if (err)
		return ERR_PTR(err);
	err = fat_chain_add(inode, cluster, 1);
	if (err) {
		fat_free_clusters(inode, cluster);
		return ERR_PTR(err);
	}

	sector = fat_clus_to_blknr(MSDOS_SB(sb), cluster);
	last_sector = sector + sec_per_clus;
	for ( ; sector < last_sector; sector++) {
		if ((bh = sb_getblk(sb, sector))) {
			memset(bh->b_data, 0, sb->s_blocksize);
			set_buffer_uptodate(bh);
			mark_buffer_dirty(bh);
			if (sb->s_flags & MS_SYNCHRONOUS)
				sync_dirty_buffer(bh);
			if (!res)
				res = bh;
			else
				brelse(bh);
		}
	}
	if (res == NULL)
		res = ERR_PTR(-EIO);
	if (inode->i_size & (sb->s_blocksize - 1)) {
		fat_fs_panic(sb, "Odd directory size");
		inode->i_size = (inode->i_size + sb->s_blocksize)
			& ~((loff_t)sb->s_blocksize - 1);
	}
	inode->i_size += MSDOS_SB(sb)->cluster_size;
	MSDOS_I(inode)->mmu_private += MSDOS_SB(sb)->cluster_size;

	return res;
}

/* This assumes that size of cluster is above the 32*slots */

int fat_add_entries(struct inode *dir,int slots, struct buffer_head **bh,
		  struct msdos_dir_entry **de, loff_t *i_pos)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset, curr;
	int row;
	struct buffer_head *new_bh;

	offset = curr = 0;
	*bh = NULL;
	row = 0;
	while (fat_get_entry(dir, &curr, bh, de, i_pos) > -1) {
		/* check the maximum size of directory */
		if (curr >= FAT_MAX_DIR_SIZE) {
			brelse(*bh);
			return -ENOSPC;
		}

		if (IS_FREE((*de)->name)) {
			if (++row == slots)
				return offset;
		} else {
			row = 0;
			offset = curr;
		}
	}
	if ((dir->i_ino == MSDOS_ROOT_INO) && (MSDOS_SB(sb)->fat_bits != 32))
		return -ENOSPC;
	new_bh = fat_extend_dir(dir);
	if (IS_ERR(new_bh))
		return PTR_ERR(new_bh);
	brelse(new_bh);
	do {
		fat_get_entry(dir, &curr, bh, de, i_pos);
	} while (++row < slots);

	return offset;
}

EXPORT_SYMBOL(fat_add_entries);
