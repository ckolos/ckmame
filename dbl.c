/*
  $NiH: dbl.c,v 1.15 2003/03/16 10:21:33 wiz Exp $

  dbl.c -- generic low level data base routines
  Copyright (C) 1999, 2003 Dieter Baron and Thomas Klausner

  This file is part of ckmame, a program to check rom sets for MAME.
  The authors can be contacted at <nih@giga.or.at>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/



#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "dbl.h"
#include "r.h"
#include "xmalloc.h"



int
ddb_insert(DB *db, char *key, DBT *value)
{
    DBT k, v;
    int ret;
    uLong len;

    k.size = strlen(key);
    k.data = xmalloc(k.size);
    strncpy(k.data, key, k.size);

    len = value->size*1.1+12;
    v.data = xmalloc(len+2);

    ((unsigned char *)v.data)[0] = (value->size >> 8) & 0xff;
    ((unsigned char *)v.data)[1] = value->size & 0xff;

    if (compress2(((unsigned char *)v.data)+2, &len, value->data, 
		  value->size, 9) != 0) {
	free(k.data);
	free(v.data);
	return -1;
    }
    v.size = len + 2;

    ret = ddb_insert_l(db, &k, &v);

    free(k.data);
    free(v.data);

    return ret;
}



int
ddb_lookup(DB *db, char *key, DBT *value)
{
    DBT k, v;
    int ret;
    uLong len;

    k.size = strlen(key);
    k.data = xmalloc(k.size);
    strncpy(k.data, key, k.size);

    ret = ddb_lookup_l(db, &k, &v);

    if (ret != 0) {
	free(k.data);
	return ret;
    }

    value->size = ((((unsigned char *)v.data)[0] << 8)
		   | (((unsigned char *)v.data)[1]));
    value->data = xmalloc(value->size);

    len = value->size;
    if (uncompress(value->data, &len, ((unsigned char *)v.data)+2, 
		   v.size-2) != 0) {
	free(value->data);
	free(k.data);
	return -1;
    }
    value->size = len;

    free(k.data);
    
    return ret;
}



char *
ddb_name(char *prefix)
{
    char *s;

    if (prefix == NULL)
	return xstrdup(DDB_FILEEXT);

    s = xmalloc(strlen(prefix)+strlen(DDB_FILEEXT)+1);
    sprintf(s, "%s%s", prefix, DDB_FILEEXT);

    return s;
}



int
ddb_check_version(DB *db, int flags)
{
    DBT v;
    int err;
    void *data;

    if (ddb_lookup(db, "/ckmame", &v) != 0) {
	if (!(flags & DDB_WRITE)) {
	    /* reading database, version not found -> old */
	    return -1;
	}
	else {
	    if (ddb_lookup(db, "/list", &v) == 0) {
		/* writing database, version not found, but list found
		   -> old */
		free(v.data);
		return -1;
	    }
	    else {
		/* writing database, version and list not found ->
		   creating database, ok */
		return 0;
	    }
	}
    }

    /* compare version numbers */

    data = v.data;
    err = (r__ushort(&v) != DDB_FORMAT_VERSION);
    free(data);
    
    return err;
}
