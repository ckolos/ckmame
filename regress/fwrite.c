/*
  fwrite.c -- override fwrite() to allow testing special cases
  Copyright (C) 2013-2015 Dieter Baron and Thomas Klausner

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define __USE_GNU
#include <dlfcn.h>
#undef __USE_GNU

static int inited = 0;
static size_t count = 0;
static size_t max_write = 0;
static size_t(*real_fwrite)(const void *ptr, size_t size, size_t nmemb, FILE * stream) = NULL;
static int(*real_link)(const char *src, const char *dest) = NULL;
static int(*real_rename)(const char *src, const char *dest) = NULL;

static FILE *logfile;

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE * stream)
{
    size_t ret;

    if (!inited) {
	char *foo;
        logfile = fopen("/tmp/fwrite.log", "a");
	if ((foo=getenv("FWRITE_MAX_WRITE")) != NULL)
	    max_write = strtoul(foo, NULL, 0);
	fprintf(logfile, "fwrite: max_write set to %lu\n", max_write);
	real_fwrite = dlsym(RTLD_NEXT, "fwrite");
	if (!real_fwrite)
	    abort();
	inited = 1;
    }
 
    if (max_write > 0 && count + size*nmemb > max_write) {
	fprintf(logfile, "fwrite: returned ENOSPC\n");
	errno = ENOSPC;
	return -1;
    }
    

    ret = real_fwrite(ptr, size, nmemb, stream);
    count += ret * size;

    fprintf(logfile, "fwrite: wrote %lu*%lu = %lu bytes, sum %lu\n", ret, size, ret * size, count);
    return ret;
}

int
rename(const char *src, const char *dest) {
    if (real_rename == NULL) {
	real_rename = dlsym(RTLD_NEXT, "rename");
	if (!real_rename)
	    abort();
    }

    if (getenv("RENAME_LOG") != NULL) {
	fprintf(stderr, "LOG: rename '%s' -> '%s'\n", src, dest);
    }

    if (getenv("RENAME_ALWAYS_FAILS") != NULL) {
	errno = EPERM;
	return -1;
    }

    if (getenv("RENAME_FAILS") != NULL) {
	if (strcmp(getenv("RENAME_FAILS"), dest) == 0) {
	    errno = EPERM;
	    return -1;
	}
    }

    return real_rename(src, dest);
}

int
link(const char *src, const char *dest) {
    if (real_link == NULL) {
	real_link = dlsym(RTLD_NEXT, "link");
	if (!real_link)
	    abort();
    }

    if (getenv("LINK_ALWAYS_FAILS") != NULL) {
	errno = EPERM;
	return -1;
    }

    if (getenv("LINK_FAILS") != NULL) {
	if (strcmp(getenv("LINK_FAILS"), dest) == 0) {
	    errno = EPERM;
	    return -1;
	}
    }

    return real_link(src, dest);
}
