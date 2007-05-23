/*
  $NiH: dbdump.c,v 1.8 2006/10/04 18:22:57 dillo Exp $

  dbdump.c -- print contents of db
  Copyright (C) 2005-2006 Dieter Baron and Thomas Klausner

  This file is part of ckmame, a program to check rom sets for MAME.
  The authors can be contacted at <ckmame@nih.at>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2, as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/



#include <stdio.h>
#include <stdlib.h>

#include "dbh.h"
#include "error.h"
#include "util.h"
#include "xmalloc.h"

const char *prg;
const char *usage = "usage: %s db-file\n";
char *buf;
size_t bufsize;



int
main(int argc, char *argv[])
{
#if 0
    DB *db;

    prg = argv[0];
    bufsize = 0;
    buf = NULL;

    if (argc != 2) {
	fprintf(stderr, usage, prg);
	exit(1);
    }

    seterrinfo(argv[1], NULL);
    if ((db=dbl_open(argv[1], DBL_READ)) == NULL) {
	myerror(ERRDB, "can't open database");
	exit(1);
    }

    if (dbl_foreach(db, dump, NULL) < 0) {
	myerror(ERRDB, "can't read all keys and values from database");
	exit(1);
    }

    /* read-only */
    dbl_close(db);
#endif
    return 0;
}
