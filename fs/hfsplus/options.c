/*
 *  linux/fs/hfsplus/options.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Option parsing
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/nls.h>
#include "hfsplus_fs.h"

enum {
	opt_creator, opt_type,
	opt_umask, opt_uid, opt_gid,
	opt_part, opt_session, opt_nls,
	opt_err
};

static match_table_t tokens = {
	{ opt_creator, "creator=%s" },
	{ opt_type, "type=%s" },
	{ opt_umask, "umask=%o" },
	{ opt_uid, "uid=%u" },
	{ opt_gid, "gid=%u" },
	{ opt_part, "part=%u" },
	{ opt_session, "session=%u" },
	{ opt_nls, "nls=%s" },
	{ opt_err, NULL }
};

/* Initialize an options object to reasonable defaults */
void fill_defaults(struct hfsplus_sb_info *opts)
{
	if (!opts)
		return;

	opts->creator = HFSPLUS_DEF_CR_TYPE;
	opts->type = HFSPLUS_DEF_CR_TYPE;
	opts->umask = current->fs->umask;
	opts->uid = current->uid;
	opts->gid = current->gid;
	opts->part = -1;
	opts->session = -1;
}

/* convert a "four byte character" to a 32 bit int with error checks */
static inline int match_fourchar(substring_t *arg, u32 *result)
{
	if (arg->to - arg->from != 4)
		return -EINVAL;
	memcpy(result, arg->from, 4);
	return 0;
}

/* Parse options from mount. Returns 0 on failure */
/* input is the options passed to mount() as a string */
int parse_options(char *input, struct hfsplus_sb_info *sbi)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int tmp, token;

	if (!input)
		goto done;

	while ((p = strsep(&input, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case opt_creator:
			if (match_fourchar(&args[0], &sbi->creator)) {
				printk("HFS+-fs: creator requires a 4 character value\n");
				return 0;
			}
			break;
		case opt_type:
			if (match_fourchar(&args[0], &sbi->type)) {
				printk("HFS+-fs: type requires a 4 character value\n");
				return 0;
			}
			break;
		case opt_umask:
			if (match_octal(&args[0], &tmp)) {
				printk("HFS+-fs: umask requires a value\n");
				return 0;
			}
			sbi->umask = (umode_t)tmp;
			break;
		case opt_uid:
			if (match_int(&args[0], &tmp)) {
				printk("HFS+-fs: uid requires an argument\n");
				return 0;
			}
			sbi->uid = (uid_t)tmp;
			break;
		case opt_gid:
			if (match_int(&args[0], &tmp)) {
				printk("HFS+-fs: gid requires an argument\n");
				return 0;
			}
			sbi->gid = (gid_t)tmp;
			break;
		case opt_part:
			if (match_int(&args[0], &sbi->part)) {
				printk("HFS+-fs: part requires an argument\n");
				return 0;
			}
			break;
		case opt_session:
			if (match_int(&args[0], &sbi->session)) {
				printk("HFS+-fs: session requires an argument\n");
				return 0;
			}
			break;
		case opt_nls:
			if (sbi->nls) {
				printk("HFS+-fs: unable to change nls mapping\n");
				return 0;
			}
			p = match_strdup(&args[0]);
			sbi->nls = load_nls(p);
			if (!sbi->nls) {
				printk("HFS+-fs: unable to load nls mapping \"%s\"\n", p);
				kfree(p);
				return 0;
			}
			kfree(p);
			break;
		default:
			return 0;
		}
	}

done:
	if (!sbi->nls) {
		/* try utf8 first, as this is the old default behaviour */
		sbi->nls = load_nls("utf8");
		if (!sbi->nls)
			sbi->nls = load_nls_default();
		if (!sbi->nls)
			return 0;
	}

	return 1;
}
