/*********************************************************************
 *                
 * Filename:      irias_object.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Thu Oct  1 22:49:50 1998
 * Modified at:   Sun Oct 25 00:28:56 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#ifndef LM_IAS_OBJECT_H
#define LM_IAS_OBJECT_H

#include "irqueue.h"

/* LM-IAS Attribute types */
#define IAS_MISSING 0
#define IAS_INTEGER 1
#define IAS_OCT_SEQ 2
#define IAS_STRING  3

/*
 *  LM-IAS Object
 */
struct ias_object {
	QUEUE queue;     /* Must be first! */

	int magic;
	
	char  *name;
	int   id;

	hashbin_t *attribs;
};

/*
 *  Values used by LM-IAS attributes
 */
struct ias_value {
	/* Value description */
        __u8    type;
	int     charset; /* Only used by string type */
        int     len;
	
	/* Value */
	union {
		int integer;
		char *string;
		__u8 *oct_seq;
	} t;
};

/*
 *  Attributes used by LM-IAS objects
 */
struct ias_attrib {
	QUEUE queue;     /* Must be first! */

	int magic;

	/* Attribute name */
        char *name;
 /*        char *attr; What do we need this for?  */

	/* Attribute value */
	struct ias_value *value;
};

char *strdup( char *str);

struct ias_object *irias_new_object( char *name, int id);
void irias_insert_object( struct ias_object *obj);
void __irias_delete_object( struct ias_object *obj);
void irias_delete_object( char *name);

void irias_add_integer_attrib( struct ias_object *obj, char *name, 
			       int value);
void irias_add_string_attrib( struct ias_object *obj, char *name, 
			      char *value);
void irias_add_octseq_attrib( struct ias_object *obj, char *name, 
			      __u8 *octets, int len);
struct ias_object *irias_find_object( char *name);
struct ias_attrib *irias_find_attrib( struct ias_object *obj, char *name);

struct ias_value *irias_new_string_value( char *string);
struct ias_value *irias_new_integer_value( int integer);
struct ias_value *irias_new_octseq_value( __u8 *octseq , int len);
void irias_delete_value( struct ias_value *value);

extern struct ias_value missing;
extern hashbin_t *objects;

#endif
