#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

#define xstr(s) #s
#define str(s) xstr(s)

static unsigned int offset;
static unsigned int ino = 721;

struct file_type {
	const char *type;
	int (*handler)(const char *line);
};

static void push_string(const char *name)
{
	unsigned int name_len = strlen(name) + 1;

	fputs(name, stdout);
	putchar(0);
	offset += name_len;
}

static void push_pad (void)
{
	while (offset & 3) {
		putchar(0);
		offset++;
	}
}

static void push_rest(const char *name)
{
	unsigned int name_len = strlen(name) + 1;
	unsigned int tmp_ofs;

	fputs(name, stdout);
	putchar(0);
	offset += name_len;

	tmp_ofs = name_len + 110;
	while (tmp_ofs & 3) {
		putchar(0);
		offset++;
		tmp_ofs++;
	}
}

static void push_hdr(const char *s)
{
	fputs(s, stdout);
	offset += 110;
}

static void cpio_trailer(void)
{
	char s[256];
	const char name[] = "TRAILER!!!";

	sprintf(s, "%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		"070701",		/* magic */
		0,			/* ino */
		0,			/* mode */
		(long) 0,		/* uid */
		(long) 0,		/* gid */
		1,			/* nlink */
		(long) 0,		/* mtime */
		0,			/* filesize */
		0,			/* major */
		0,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		(unsigned)strlen(name) + 1, /* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_rest(name);

	while (offset % 512) {
		putchar(0);
		offset++;
	}
}

static int cpio_mkdir(const char *name, unsigned int mode,
		       uid_t uid, gid_t gid)
{
	char s[256];
	time_t mtime = time(NULL);

	sprintf(s,"%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		"070701",		/* magic */
		ino++,			/* ino */
		S_IFDIR | mode,		/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		2,			/* nlink */
		(long) mtime,		/* mtime */
		0,			/* filesize */
		3,			/* major */
		1,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		(unsigned)strlen(name) + 1,/* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_rest(name);
	return 0;
}

static int cpio_mkdir_line(const char *line)
{
	char name[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;
	int rc = -1;

	if (4 != sscanf(line, "%" str(PATH_MAX) "s %o %d %d", name, &mode, &uid, &gid)) {
		fprintf(stderr, "Unrecognized dir format '%s'", line);
		goto fail;
	}
	rc = cpio_mkdir(name, mode, uid, gid);
 fail:
	return rc;
}

static int cpio_mknod(const char *name, unsigned int mode,
		       uid_t uid, gid_t gid, char dev_type,
		       unsigned int maj, unsigned int min)
{
	char s[256];
	time_t mtime = time(NULL);

	if (dev_type == 'b')
		mode |= S_IFBLK;
	else
		mode |= S_IFCHR;

	sprintf(s,"%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		"070701",		/* magic */
		ino++,			/* ino */
		mode,			/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		1,			/* nlink */
		(long) mtime,		/* mtime */
		0,			/* filesize */
		3,			/* major */
		1,			/* minor */
		maj,			/* rmajor */
		min,			/* rminor */
		(unsigned)strlen(name) + 1,/* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_rest(name);
	return 0;
}

static int cpio_mknod_line(const char *line)
{
	char name[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;
	char dev_type;
	unsigned int maj;
	unsigned int min;
	int rc = -1;

	if (7 != sscanf(line, "%" str(PATH_MAX) "s %o %d %d %c %u %u",
			 name, &mode, &uid, &gid, &dev_type, &maj, &min)) {
		fprintf(stderr, "Unrecognized nod format '%s'", line);
		goto fail;
	}
	rc = cpio_mknod(name, mode, uid, gid, dev_type, maj, min);
 fail:
	return rc;
}

/* Not marked static to keep the compiler quiet, as no one uses this yet... */
static int cpio_mkfile(const char *name, const char *location,
			unsigned int mode, uid_t uid, gid_t gid)
{
	char s[256];
	char *filebuf = NULL;
	struct stat buf;
	int file = -1;
	int retval;
	int i;
	int rc = -1;

	mode |= S_IFREG;

	retval = stat (location, &buf);
	if (retval) {
		fprintf (stderr, "File %s could not be located\n", location);
		goto error;
	}

	file = open (location, O_RDONLY);
	if (file < 0) {
		fprintf (stderr, "File %s could not be opened for reading\n", location);
		goto error;
	}

	filebuf = malloc(buf.st_size);
	if (!filebuf) {
		fprintf (stderr, "out of memory\n");
		goto error;
	}

	retval = read (file, filebuf, buf.st_size);
	if (retval < 0) {
		fprintf (stderr, "Can not read %s file\n", location);
		goto error;
	}

	sprintf(s,"%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		"070701",		/* magic */
		ino++,			/* ino */
		mode,			/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		1,			/* nlink */
		(long) buf.st_mtime,	/* mtime */
		(int) buf.st_size,	/* filesize */
		3,			/* major */
		1,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		(unsigned)strlen(name) + 1,/* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_string(name);
	push_pad();

	for (i = 0; i < buf.st_size; ++i)
		fputc(filebuf[i], stdout);
	offset += buf.st_size;
	push_pad();
	rc = 0;
	
error:
	if (filebuf) free(filebuf);
	if (file >= 0) close(file);
	return rc;
}

static int cpio_mkfile_line(const char *line)
{
	char name[PATH_MAX + 1];
	char location[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;
	int rc = -1;

	if (5 != sscanf(line, "%" str(PATH_MAX) "s %" str(PATH_MAX) "s %o %d %d", name, location, &mode, &uid, &gid)) {
		fprintf(stderr, "Unrecognized file format '%s'", line);
		goto fail;
	}
	rc = cpio_mkfile(name, location, mode, uid, gid);
 fail:
	return rc;
}

void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n"
		"\t%s <cpio_list>\n"
		"\n"
		"<cpio_list> is a file containing newline separated entries that\n"
		"describe the files to be included in the initramfs archive:\n"
		"\n"
		"# a comment\n"
		"file <name> <location> <mode> <uid> <gid> \n"
		"dir <name> <mode> <uid> <gid>\n"
		"nod <name> <mode> <uid> <gid> <dev_type> <maj> <min>\n"
		"\n"
		"<name>      name of the file/dir/nod in the archive\n"
		"<location>  location of the file in the current filesystem\n"
		"<mode>      mode/permissions of the file\n"
		"<uid>       user id (0=root)\n"
		"<gid>       group id (0=root)\n"
		"<dev_type>  device type (b=block, c=character)\n"
		"<maj>       major number of nod\n"
		"<min>       minor number of nod\n"
		"\n"
		"example:\n"
		"# A simple initramfs\n"
		"dir /dev 0755 0 0\n"
		"nod /dev/console 0600 0 0 c 5 1\n"
		"dir /root 0700 0 0\n"
		"dir /sbin 0755 0 0\n"
		"file /sbin/kinit /usr/src/klibc/kinit/kinit 0755 0 0\n",
		prog);
}

struct file_type file_type_table[] = {
	{
		.type    = "file",
		.handler = cpio_mkfile_line,
	}, {
		.type    = "nod",
		.handler = cpio_mknod_line,
	}, {
		.type    = "dir",
		.handler = cpio_mkdir_line,
	}, {
		.type    = NULL,
		.handler = NULL,
	}
};

#define LINE_SIZE (2 * PATH_MAX + 50)

int main (int argc, char *argv[])
{
	FILE *cpio_list;
	char line[LINE_SIZE];
	char *args, *type;
	int ec = 0;
	int line_nr = 0;

	if (2 != argc) {
		usage(argv[0]);
		exit(1);
	}

	if (! (cpio_list = fopen(argv[1], "r"))) {
		fprintf(stderr, "ERROR: unable to open '%s': %s\n\n",
			argv[1], strerror(errno));
		usage(argv[0]);
		exit(1);
	}

	while (fgets(line, LINE_SIZE, cpio_list)) {
		int type_idx;
		size_t slen = strlen(line);

		line_nr++;

		if ('#' == *line) {
			/* comment - skip to next line */
			continue;
		}

		if (! (type = strtok(line, " \t"))) {
			fprintf(stderr,
				"ERROR: incorrect format, could not locate file type line %d: '%s'\n",
				line_nr, line);
			ec = -1;
		}

		if ('\n' == *type) {
			/* a blank line */
			continue;
		}

		if (slen == strlen(type)) {
			/* must be an empty line */
			continue;
		}

		if (! (args = strtok(NULL, "\n"))) {
			fprintf(stderr,
				"ERROR: incorrect format, newline required line %d: '%s'\n",
				line_nr, line);
			ec = -1;
		}

		for (type_idx = 0; file_type_table[type_idx].type; type_idx++) {
			int rc;
			if (! strcmp(line, file_type_table[type_idx].type)) {
				if ((rc = file_type_table[type_idx].handler(args))) {
					ec = rc;
					fprintf(stderr, " line %d\n", line_nr);
				}
				break;
			}
		}

		if (NULL == file_type_table[type_idx].type) {
			fprintf(stderr, "unknown file type line %d: '%s'\n",
				line_nr, line);
		}
	}
	cpio_trailer();

	exit(ec);
}
