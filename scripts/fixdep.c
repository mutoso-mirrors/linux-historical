/*
 * "Optimize" a list of dependencies as spit out by gcc -MD 
 * for the kernel build
 * ===========================================================================
 *
 * Author       Kai Germaschewski
 * Copyright    2002 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 *
 * Introduction:
 * 
 * gcc produces a very nice and correct list of dependencies which
 * tells make when to remake a file.
 *
 * To use this list as-is however has the drawback that virtually
 * every file in the kernel includes <linux/config.h> which then again
 * includes <linux/autoconf.h>
 *
 * If the user re-runs make *config, linux/autoconf.h will be
 * regenerated.  make notices that and will rebuild every file which
 * includes autconf.h, i.e. basically all files. This is extremely
 * annoying if the user just changed CONFIG_HIS_DRIVER from n to m.
 * 
 * So we play the same trick that "mkdep" played before. We replace
 * the dependency on linux/autoconf.h by a dependency on every config
 * option which is mentioned in any of the listed prequisites.
 *  
 * To be exact, split-include populates a tree in include/config/,
 * e.g. include/config/his/driver.h, which contains the #define/#undef
 * for the CONFIG_HIS_DRIVER option.
 *
 * So if the user changes his CONFIG_HIS_DRIVER option, only the objects
 * which depend on "include/linux/config/his/driver.h" will be rebuilt,
 * so most likely only his driver ;-) 
 *
 * The idea above dates, by the way, back to Michael E Chastain, AFAIK.
 * 
 * So to get dependencies right, there two issues:
 * o if any of the files the compiler read changed, we need to rebuild
 * o if the command line given to the compile the file changed, we
 *   better rebuild as well.
 *
 * The former is handled by using the -MD output, the later by saving
 * the command line used to compile the old object and comparing it
 * to the one we would now use.
 *
 * Again, also this idea is pretty old and has been discussed on
 * kbuild-devel a long time ago. I don't have a sensibly working
 * internet connection right now, so I rather don't mention names
 * without double checking.
 *
 * This code here has been based partially based on mkdep.c, which
 * says the following about its history:
 *
 *   Copyright abandoned, Michael Chastain, <mailto:mec@shout.net>.
 *   This is a C version of syncdep.pl by Werner Almesberger.
 *
 *
 * It is invoked as
 *
 *   fixdep <target> <topdir> <cmdline>
 *
 * and will read the dependency file ".<target>.d".
 *
 * The transformed dependency snipped is written to stdout.
 *
 * It first generates a line
 *
 *   cmd_<target> = <cmdline>
 *
 * and then basically copies the .<target>.d file to stdout, in the
 * process filtering out the dependency on linux/autconf.h and adding
 * dependencies on include/config/my/option.h for every
 * CONFIG_MY_OPTION encountered in any of the prequisites.
 *
 * It will also filter out all the dependencies on *.ver. We need
 * to make sure that the generated version checksum are globally up
 * to date before even starting the recursive build, so it's too late
 * at this point anyway.
 *
 * The algorithm to grep for "CONFIG_..." is bit unusual, but should
 * be fast ;-) We don't even try to really parse the header files, but
 * merely grep, i.e. if CONFIG_FOO is mentioned in a comment, it will
 * be picked up as well. It's not a problem with respect to
 * correctness, since that can only give too many dependencies, thus
 * we cannot miss a rebuild. Since people tend to not mention totally
 * unrelated CONFIG_ options all over the place, it's not an
 * efficiency problem either.
 * 
 * (Note: it'd be easy to port over the complete mkdep state machine,
 *  but I don't think the added complexity is worth it)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <netinet/in.h>

#define INT_CONF ntohl(0x434f4e46)
#define INT_ONFI ntohl(0x4f4e4649)
#define INT_NFIG ntohl(0x4e464947)
#define INT_FIG_ ntohl(0x4649475f)

char *topdir;

void usage(void)

{
	fprintf(stderr, "Usage: fixdep <target> <topdir> <cmdline>\n");
	exit(1);
}

void print_cmdline(char *target, char *cmdline)
{
	char *s = strdup(target);
	char *p = s;

	if (!s) {
		fprintf(stderr, "no mem!\n");
		exit(2);
	}
	while ((p = strchr(p,'/')))
		*p = '_';

	printf("cmd_%s := %s\n\n", s, cmdline);

	free(s);
}

char * str_config  = NULL;
int    size_config = 0;
int    len_config  = 0;

/*
 * Grow the configuration string to a desired length.
 * Usually the first growth is plenty.
 */
void grow_config(int len)
{
	while (len_config + len > size_config) {
		if (size_config == 0)
			size_config = 2048;
		str_config = realloc(str_config, size_config *= 2);
		if (str_config == NULL)
			{ perror("malloc"); exit(1); }
	}
}



/*
 * Lookup a value in the configuration string.
 */
int is_defined_config(const char * name, int len)
{
	const char * pconfig;
	const char * plast = str_config + len_config - len;
	for ( pconfig = str_config + 1; pconfig < plast; pconfig++ ) {
		if (pconfig[ -1] == '\n'
		&&  pconfig[len] == '\n'
		&&  !memcmp(pconfig, name, len))
			return 1;
	}
	return 0;
}

/*
 * Add a new value to the configuration string.
 */
void define_config(const char * name, int len)
{
	grow_config(len + 1);

	memcpy(str_config+len_config, name, len);
	len_config += len;
	str_config[len_config++] = '\n';
}

/*
 * Clear the set of configuration strings.
 */
void clear_config(void)
{
	len_config = 0;
	define_config("", 0);
}

/*
 * Record the use of a CONFIG_* word.
 */
void use_config(char *m, int slen)
{
	char s[PATH_MAX];
	char *p;

	if (is_defined_config(m, slen))
	    return;

	define_config(m, slen);

	memcpy(s, m, slen); s[slen] = 0;

	for (p = s; p < s + slen; p++) {
		if (*p == '_')
			*p = '/';
		else
			*p = tolower((unsigned char)*p);
	}
	printf("    $(wildcard %s/include/config/%s.h) \\\n", topdir, s);
}

void parse_config_file(char *map, size_t len)
{
	int *end = (int *) (map + len);
	// start at +1, so that p can never be < map
	int *m   = (int *) map + 1;
	char *p, *q;

	for (; m < end; m++) {
		if (*m == INT_CONF) { p = (char *) m  ; goto conf; }
		if (*m == INT_ONFI) { p = (char *) m-1; goto conf; }
		if (*m == INT_NFIG) { p = (char *) m-2; goto conf; }
		if (*m == INT_FIG_) { p = (char *) m-3; goto conf; }
		continue;
	conf:
		if (p > map + len - 7)
			continue;
		if (memcmp(p, "CONFIG_", 7))
			continue;
		for (q = p + 7; q < map + len; q++) {
			if (!(isalnum(*q)))
				goto found;
		}
		continue;

	found: 
		use_config(p+7, q-p-7);
	}
}

/* test is s ends in sub */
int strrcmp(char *s, char *sub)
{
	int slen = strlen(s);
	int sublen = strlen(sub);
  
	if (sublen > slen)
		return 1;
	
	return memcmp(s + slen - sublen, sub, sublen);
}

void do_config_file(char *filename)
{
	struct stat st;
	int fd;
	void *map;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		exit(2);
	}
	fstat(fd, &st);
	if (st.st_size == 0) {
		close(fd);
		return;
	}
	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if ((long) map == -1) {
		perror("mmap");
		close(fd);
		return;
	}
	
	parse_config_file(map, st.st_size);

	munmap(map, st.st_size);
}

void parse_dep_file(void *map, size_t len)
{
	char *m = map;
	char *end = map + len;
	char *p;
	char s[PATH_MAX];

	p = strchr(m, ':');
	if (!p) {
		fprintf(stderr, "parse error at %d", __LINE__);
		exit(1);
	}
	memcpy(s, m, p-m); s[p-m] = 0;
	printf("%s: \\\n", s);
	m = p+1;

	clear_config();

	while (m < end) {
		while (*m == ' ' || *m == '\\' || *m == '\n')
			m++;

		p = strchr(m, ' ');
		if (!p) {
			p = end;
			while (!isalpha(*p)) p--;
			p++;
		}
		memcpy(s, m, p-m); s[p-m] = 0;
		if (strrcmp(s, "include/linux/autoconf.h") &&
		    strrcmp(s, ".ver")) {
			printf("  %s \\\n", s);
			do_config_file(s);
		}
		m = p + 1;
	}
	printf("\n");
}

void print_deps(char *target)
{
	char filename[PATH_MAX];
	struct stat st;
	int fd;
	void *map;

	sprintf(filename, ".%s.d", target);
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		exit(2);
	}
	fstat(fd, &st);
	if (st.st_size == 0) {
		fprintf(stderr,"%s is empty\n",filename);
		close(fd);
		return;
	}
	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if ((long) map == -1) {
		perror("mmap");
		close(fd);
		return;
	}
	
	parse_dep_file(map, st.st_size);

	munmap(map, st.st_size);
}

void traps(void)
{
	char *test = "CONF";

	if (*(int *)test != INT_CONF) {
		fprintf(stderr, "sizeof(int) != 4 or wrong endianess? %#x\n",
			*(int *)test);
		exit(2);
	}
}

int main(int argc, char *argv[])
{
	char *target, *cmdline;
	
	traps();

	if (argc != 4)
		usage();
		
	target = argv[1];
	topdir = argv[2];
	cmdline = argv[3];

	print_cmdline(target, cmdline);
	print_deps(target);

	return 0;
}
