/*
 * conmakehash.c
 *
 * Create a pre-initialized kernel Unicode hash table
 *
 * Copyright (C) 1995 H. Peter Anvin
 *
 * This program may be freely copied under the terms of the GNU
 * General Public License (GPL), version 2, or at your option
 * any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>

typedef unsigned short unicode;

struct unipair
{
  unsigned short glyph;		/* Glyph code */
  unicode uc;			/* Unicode listed */
};

void usage(char *argv0)
{
  fprintf(stderr, "Usage: \n"
         "        %s chartable [hashsize] [hashstep] [maxhashlevel]\n", argv0);
  exit(EX_USAGE);
}

int getunicode(char **p0)
{
  char *p = *p0;

  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != 'U' || p[1] != '+' ||
      !isxdigit(p[2]) || !isxdigit(p[3]) || !isxdigit(p[4]) ||
      !isxdigit(p[5]) || isxdigit(p[6]))
    return -1;
  *p0 = p+6;
  return strtol(p+2,0,16);
}

struct unipair *hashtable;
int hashsize = 641;		/* Size of hash table */
int hashstep = 189;		/* Hash stepping */
int maxhashlevel = 6;		/* Maximum hash depth */
int hashlevel = 0;		/* Actual hash depth */

void addpair(int fp, int un)
{
  int i, lct;
  unicode hu;

  if ( un <= 0xFFFE )
    {
      /* Add to hash table */

      i = un % hashsize;
      lct = 1;
      
      while ( (hu = hashtable[i].uc) != 0xffff && hu != un )
	{
	  if (lct++ >= maxhashlevel)
	    {
	      fprintf(stderr, "ERROR: Hash table overflow\n");
	      exit(EX_DATAERR);
	    }
	  i += hashstep;
	  if ( i >= hashsize )
	    i -= hashsize;
	}
      if ( lct > hashlevel )
	hashlevel = lct;

      hashtable[i].uc = un;
      hashtable[i].glyph = fp;
    }

  /* otherwise: ignore */
}

int main(int argc, char *argv[])
{
  FILE *ctbl;
  char *tblname;
  char buffer[65536];
  int fontlen;
  int i;
  int fp0, fp1, un0, un1;
  char *p, *p1;

  if ( argc < 2 || argc > 5 )
    usage(argv[0]);

  if ( !strcmp(argv[1],"-") )
    {
      ctbl = stdin;
      tblname = "stdin";
    }
  else
    {
      ctbl = fopen(tblname = argv[1], "r");
      if ( !ctbl )
	{
	  perror(tblname);
	  exit(EX_NOINPUT);
	}
    }

  if ( argc > 2 )
    {
      hashsize = atoi(argv[2]);
      if ( hashsize < 256 || hashsize > 2048 )
	{
	  fprintf(stderr, "Illegal hash size\n");
	  exit(EX_USAGE);
	}
    }
  
  if ( argc > 3 )
    {
      hashstep = atoi(argv[3]) % hashsize;
      if ( hashstep < 0 ) hashstep += hashsize;
      if ( hashstep < 16 || hashstep >= hashsize-16 )
	{
	  fprintf(stderr, "Bad hash step\n");
	  exit(EX_USAGE);
	}
    }

  /* Warn the user in case the hashstep and hashsize are not relatively
     prime -- this algorithm could be massively improved */

  for ( i = hashstep ; i > 1 ; i-- )
    {
      if ( hashstep % i == 0 && hashsize % i == 0 )
	break;			/* Found GCD */
    }

  if ( i > 1 )
    {
      fprintf(stderr,
      "WARNING: hashsize and hashstep have common factors (gcd = %d)\n", i);
    }

  if ( argc > 4 )
    {
      maxhashlevel = atoi(argv[4]);
      if ( maxhashlevel < 1 || maxhashlevel > hashsize )
	{
	  fprintf(stderr, "Illegal max hash level\n");
	  exit(EX_USAGE);
	}
    }

  /* For now we assume the default font is always 256 characters */
  fontlen = 256;

  /* Initialize hash table */

  hashtable = malloc(hashsize * sizeof(struct unipair));
  if ( !hashtable )
    {
      fprintf(stderr, "Could not allocate memory for hash table\n");
      exit(EX_OSERR);
    }

  for ( i = 0 ; i < hashsize ; i++ )
    {
      hashtable[i].uc = 0xffff;
      hashtable[i].glyph = 0;
    }

  /* Now we come to the tricky part.  Parse the input table. */

  while ( fgets(buffer, sizeof(buffer), ctbl) != NULL )
    {
      if ( (p = strchr(buffer, '\n')) != NULL )
	*p = '\0';
      else
	fprintf(stderr, "%s: Warning: line too long\n", tblname);

      p = buffer;

/*
 * Syntax accepted:
 *	<fontpos>	<unicode> <unicode> ...
 *	<range>		idem
 *	<range>		<unicode range>
 *
 * where <range> ::= <fontpos>-<fontpos>
 * and <unicode> ::= U+<h><h><h><h>
 * and <h> ::= <hexadecimal digit>
 */

      while (*p == ' ' || *p == '\t')
	p++;
      if (!*p || *p == '#')
	continue;	/* skip comment or blank line */

      fp0 = strtol(p, &p1, 0);
      if (p1 == p)
	{
	  fprintf(stderr, "Bad input line: %s\n", buffer);
	  exit(EX_DATAERR);
        }
      p = p1;

      while (*p == ' ' || *p == '\t')
	p++;
      if (*p == '-')
	{
	  p++;
	  fp1 = strtol(p, &p1, 0);
	  if (p1 == p)
	    {
	      fprintf(stderr, "Bad input line: %s\n", buffer);
	      exit(EX_DATAERR);
	    }
	  p = p1;
        }
      else
	fp1 = 0;

      if ( fp0 < 0 || fp0 >= fontlen )
	{
	    fprintf(stderr,
		    "%s: Glyph number (0x%x) larger than font length\n",
		    tblname, fp0);
	    exit(EX_DATAERR);
	}
      if ( fp1 && (fp1 < fp0 || fp1 >= fontlen) )
	{
	    fprintf(stderr,
		    "%s: Bad end of range (0x%x)\n",
		    tblname, fp1);
	    exit(EX_DATAERR);
	}

      if (fp1)
	{
	  /* we have a range; expect the word "idem" or a Unicode range of the
	     same length */
	  while (*p == ' ' || *p == '\t')
	    p++;
	  if (!strncmp(p, "idem", 4))
	    {
	      for (i=fp0; i<=fp1; i++)
		addpair(i,i);
	      p += 4;
	    }
	  else
	    {
	      un0 = getunicode(&p);
	      while (*p == ' ' || *p == '\t')
		p++;
	      if (*p != '-')
		{
		  fprintf(stderr,
"%s: Corresponding to a range of font positions, there should be a Unicode range\n",
			  tblname);
		  exit(EX_DATAERR);
	        }
	      p++;
	      un1 = getunicode(&p);
	      if (un0 < 0 || un1 < 0)
		{
		  fprintf(stderr,
"%s: Bad Unicode range corresponding to font position range 0x%x-0x%x\n",
			  tblname, fp0, fp1);
		  exit(EX_DATAERR);
	        }
	      if (un1 - un0 != fp1 - fp0)
		{
		  fprintf(stderr,
"%s: Unicode range U+%x-U+%x not of the same length as font position range 0x%x-0x%x\n",
			  tblname, un0, un1, fp0, fp1);
		  exit(EX_DATAERR);
	        }
	      for(i=fp0; i<=fp1; i++)
		addpair(i,un0-fp0+i);
	    }
        }
      else
	{
	    /* no range; expect a list of unicode values for a single font position */

	    while ( (un0 = getunicode(&p)) >= 0 )
	      addpair(fp0, un0);
	}
      while (*p == ' ' || *p == '\t')
	p++;
      if (*p && *p != '#')
	fprintf(stderr, "%s: trailing junk (%s) ignored\n", tblname, p);
    }

  /* Okay, we hit EOF, now output hash table */
  
  fclose(ctbl);
  
  printf("\
/*\n\
 * uni_hash.tbl\n\
 *\n\
 * Do not edit this file; it was automatically generated by\n\
 *\n\
 * conmakehash %s %d %d %d > uni_hash.tbl\n\
 *\n\
 */\n\
\n\
#include <linux/kd.h>\n\
\n\
#define HASHSIZE      %d\n\
#define HASHSTEP      %d\n\
#define MAXHASHLEVEL  %d\n\
#define DEF_HASHLEVEL %d\n\
\n\
static unsigned int hashsize     = HASHSIZE;\n\
static unsigned int hashstep     = HASHSTEP;\n\
static unsigned int maxhashlevel = MAXHASHLEVEL;\n\
static unsigned int hashlevel    = DEF_HASHLEVEL;\n\
\n\
static struct unipair hashtable[HASHSIZE] =\n\
{\n\t", argv[1], hashsize, hashstep, maxhashlevel,
	 hashsize, hashstep, maxhashlevel, hashlevel);
  
  for ( i = 0 ; i < hashsize ; i++ )
    {
      printf("{0x%04x,0x%02x}", hashtable[i].uc, hashtable[i].glyph);
      if ( i == hashsize-1 )
	printf("\n};\n");
      else if ( i % 4 == 3 )
	printf(",\n\t");
      else
	printf(", ");
    }

  printf("\n\
#ifdef NEED_BACKUP_HASHTABLE\n\
\n\
static const struct unipair backup_hashtable[HASHSIZE] = \n{\n\t");
 
  for ( i = 0 ; i < hashsize ; i++ )
    {
      printf("{0x%04x,0x%02x}", hashtable[i].uc, hashtable[i].glyph);
      if ( i == hashsize-1 )
	printf("\n};\n#endif\n");
      else if ( i % 4 == 3 )
	printf(",\n\t");
      else
	printf(", ");
    }

  exit(EX_OK);
}
