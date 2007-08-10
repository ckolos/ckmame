/*
  util.c -- utility functions needed only by ckmame itself
  Copyright (C) 1999-2007 Dieter Baron and Thomas Klausner

  This file is part of ckmame, a program to check rom sets for MAME.
  The authors can be contacted at <ckmame@nih.at>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
  3. The name of the author may not be used to endorse or promote
     products derived from this software without specific prior
     written permission.
 
  THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "error.h"
#include "funcs.h"
#include "globals.h"
#include "util.h"
#include "xmalloc.h"



#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif



int
copy_file(const char *old, const char *new)
{
    FILE *fin, *fout;
    char b[8192], nr, nw, n;
    int err;

    if ((fin=fopen(old, "rb")) == NULL)
	return -1;

    if ((fout=fopen(new, "wb")) == NULL) {
	fclose(fin);
	return -1;
    }

    while ((nr=fread(b, sizeof(b), 1, fin)) > 0) {
	nw = 0;
	while (nw<nr) {
	    if ((n=fwrite(b+nw, nr-nw, 1, fout)) == 0) {
		err = errno;
		fclose(fin);
		fclose(fout);
		remove(new);
		errno = err;
		return -1;
	    }
	    nw += n;
	}
    }

    if (fclose(fout) != 0 || ferror(fin)) {
	err = errno;
	fclose(fin);
	unlink(new);
	errno = err;
	return -1;
    }

    fclose(fin);
    return 0;
}



int
ensure_dir(const char *name, int strip_fname)
{
    const char *p;
    char *dir;
    struct stat st;
    int ret;

    if (strip_fname) {
	p = strrchr(name, '/');
	if (p == NULL)
	    dir = xstrdup(".");
	else {
	    dir = xmalloc(p-name+1);
	    strncpy(dir, name, p-name);
	    dir[p-name] = 0;
	}
	name = dir;
    }

    ret = 0;
    if (stat(name, &st) < 0) {
	if (mkdir(name, 0777) < 0) {
	    myerror(ERRSTR, "mkdir `%s' failed", name);
	    ret = -1;
	}
    }
    else if (!(st.st_mode & S_IFDIR)) {
	myerror(ERRDEF, "`%s' is not a directory", name);
	ret = -1;
    }

    if (strip_fname)
	free(dir);

    return ret;
}		    



int
link_or_copy(const char *old, const char *new)
{
    if (link(old, new) < 0) {
	if (copy_file(old, new) < 0) {
	    seterrinfo(old, NULL);
	    myerror(ERRFILESTR, "cannot link to `%s'", new);
	    return -1;
	}
    }

    return 0;
}



char *
make_garbage_name(const char *name, int unique)
{
    const char *s;
    char *t, *u, *ext;
    struct stat st;

    s = mybasename(name);

    t = (char *)xmalloc(strlen(unknown_dir)+strlen(s)+2);

    sprintf(t, "%s/%s", unknown_dir, s);

    if (unique && (stat(t, &st) == 0 || errno != ENOENT)) {
	ext = strchr(t, '.');
	if (ext) 
	    *(ext++) = '\0';
	else
	    ext = "";
	u = make_unique_name(ext, "%s", t);
	free(t);
	return u;
    }

    return t;
}



char *
make_unique_name(const char *ext, const char *fmt, ...)
{
    char ret[MAXPATHLEN];
    int i, len;
    struct stat st;
    va_list ap;

    va_start(ap, fmt);
    len = vsnprintf(ret, sizeof(ret), fmt, ap);
    va_end(ap);

    /* already used space, "-XXX.", extension, 0 */
    if (len+5+strlen(ext)+1 > sizeof(ret)) {
	return NULL;
    }

    for (i=0; i<1000; i++) {
	sprintf(ret+len, "-%03d%s%s", i, (ext[0] ? "." : ""), ext);

	if (stat(ret, &st) == -1 && errno == ENOENT)
	    return xstrdup(ret);
    }
    
    return NULL;
}



char *
make_needed_name(const file_t *r)
{
    char crc[HASHES_SIZE_CRC*2+1];

    /* <needed_dir>/<crc>-nnn.zip */

    hash_to_string(crc, HASHES_TYPE_CRC, file_hashes(r));

    return make_unique_name("zip", "%s/%s", needed_dir, crc);
}



char *
make_needed_name_disk(const disk_t *d)
{
    char md5[HASHES_SIZE_MD5*2+1];

    /* <needed_dir>/<md5>-nnn.zip */

    hash_to_string(md5, HASHES_TYPE_MD5, disk_hashes(d));

    return make_unique_name("chd", "%s/%s", needed_dir, md5);
}



int
move_image_to_garbage(const char *fname)
{
    char *to_name;
    int ret;

    to_name = make_garbage_name(fname, 1);
    ensure_dir(to_name, 1);
    ret = rename_or_move(fname, to_name);
    free(to_name);

    return ret;
}



int
my_remove(const char *name)
{
    if (remove(name) != 0) {
	seterrinfo(name, NULL);
	myerror(ERRFILESTR, "cannot remove");
	return -1;
    }

    return 0;
}



int
rename_or_move(const char *old, const char *new)
{
    if (rename(old, new) < 0) {
	if (copy_file(old, new) < 0) {
	    seterrinfo(old, NULL);
	    myerror(ERRFILESTR, "cannot rename to `%s'", new);
	    return -1;
	}
	unlink(old);
    }

    return 0;
}



void
remove_empty_archive(const char *name)
{
    int idx;

    if (fix_options & FIX_PRINT)
	printf("%s: remove empty archive\n", name);
    if (superfluous) {
	idx = parray_index(superfluous, name, strcmp);
	/* "needed" zip archives are not in list */
	if (idx >= 0)
	    parray_delete(superfluous, idx, free);
    }
}



void
remove_from_superfluous(const char *name)
{
    int idx;

    if (superfluous) {
	idx = parray_index(superfluous, name, strcmp);
	/* "needed" images are not in list */
	if (idx >= 0)
	    parray_delete(superfluous, idx, free);
    }
}



int
save_needed(archive_t *sa, int sidx, int do_save)
{
    char *tmp;
    archive_t *da;

    if ((tmp=make_needed_name(archive_file(sa, sidx))) == NULL) {
	myerror(ERRDEF, "cannot create needed file name");
	return -1;
    }

    if ((da=archive_new(tmp, archive_filetype(sa), FILE_NEEDED,
			ARCHIVE_FL_CREATE
			| (do_save ? 0 : ARCHIVE_FL_RDONLY))) == NULL)
	return -1;

    free(tmp);

    
    if (archive_file_copy(sa, sidx, da, file_name(archive_file(sa, sidx))) < 0
	|| archive_commit(da) < 0) {
	archive_rollback(da);
	archive_free(da);
	return -1;
    }

    return archive_file_delete(sa, sidx);
}



int
save_needed_disk(const char *fname, int do_save)
{
    char *tmp;
    int ret;
    disk_t *d;

    if ((d=disk_new(fname, 0)) == NULL)
	return -1;

    ret = 0;
    tmp = NULL;

    if (do_save) {
	tmp = make_needed_name_disk(d);
	if (tmp == NULL) {
	    myerror(ERRDEF, "cannot create needed file name");
	    ret = -1;
	}
	else if (ensure_dir(tmp, 1) < 0)
	    ret = -1;
	else if (rename_or_move(fname, tmp) != 0)
	    ret = -1;
	else {
	    disk_free(d);
	    d = disk_new(tmp, 0);
	}
    }

    ensure_needed_maps();
    enter_disk_in_map(d, FILE_NEEDED);
    disk_free(d);
    free(tmp);
    return ret;
}
