/*
 * unistrk:  Unicode kernel case support
 *
 * Function:
 *     Convert a unicode character to upper or lower case using
 *     compressed tables.
 *
 *   Copyright (c) International Business Machines  Corp., 2000
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 */

#include <asm/byteorder.h>
#include "jfs_types.h"

typedef struct {
	wchar_t start;
	wchar_t end;
	signed char *table;
} UNICASERANGE;

extern signed char UniUpperTable[512];
extern UNICASERANGE UniUpperRange[];
extern int get_UCSname(component_t *, struct dentry *, struct nls_table *);
extern int jfs_strfromUCS_le(char *, const wchar_t *, int, struct nls_table *);

#define free_UCSname(COMP) kfree((COMP)->name)

/*
 * UniStrcpy:  Copy a string
 */
static inline wchar_t *UniStrcpy(wchar_t * ucs1, const wchar_t * ucs2)
{
	wchar_t *anchor = ucs1;	/* save the start of result string */

	while ((*ucs1++ = *ucs2++));
	return anchor;
}



/*
 * UniStrncpy:  Copy length limited string with pad
 */
static inline wchar_t *UniStrncpy(wchar_t * ucs1, const wchar_t * ucs2,
				  size_t n)
{
	wchar_t *anchor = ucs1;

	while (n-- && *ucs2)	/* Copy the strings */
		*ucs1++ = *ucs2++;

	n++;
	while (n--)		/* Pad with nulls */
		*ucs1++ = 0;
	return anchor;
}

/*
 * UniStrncmp_le:  Compare length limited string - native to little-endian
 */
static inline int UniStrncmp_le(const wchar_t * ucs1, const wchar_t * ucs2,
				size_t n)
{
	if (!n)
		return 0;	/* Null strings are equal */
	while ((*ucs1 == __le16_to_cpu(*ucs2)) && *ucs1 && --n) {
		ucs1++;
		ucs2++;
	}
	return (int) *ucs1 - (int) __le16_to_cpu(*ucs2);
}

/*
 * UniStrncpy_le:  Copy length limited string with pad to little-endian
 */
static inline wchar_t *UniStrncpy_le(wchar_t * ucs1, const wchar_t * ucs2,
				     size_t n)
{
	wchar_t *anchor = ucs1;

	while (n-- && *ucs2)	/* Copy the strings */
		*ucs1++ = __le16_to_cpu(*ucs2++);

	n++;
	while (n--)		/* Pad with nulls */
		*ucs1++ = 0;
	return anchor;
}


/*
 * UniToupper:  Convert a unicode character to upper case
 */
static inline wchar_t UniToupper(wchar_t uc)
{
	UNICASERANGE *rp;

	if (uc < sizeof(UniUpperTable)) {	/* Latin characters */
		return uc + UniUpperTable[uc];	/* Use base tables */
	} else {
		rp = UniUpperRange;	/* Use range tables */
		while (rp->start) {
			if (uc < rp->start)	/* Before start of range */
				return uc;	/* Uppercase = input */
			if (uc <= rp->end)	/* In range */
				return uc + rp->table[uc - rp->start];
			rp++;	/* Try next range */
		}
	}
	return uc;		/* Past last range */
}


/*
 * UniStrupr:  Upper case a unicode string
 */
static inline wchar_t *UniStrupr(wchar_t * upin)
{
	wchar_t *up;

	up = upin;
	while (*up) {		/* For all characters */
		*up = UniToupper(*up);
		up++;
	}
	return upin;		/* Return input pointer */
}

