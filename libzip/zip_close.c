#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include "zip.h"
#include "zipint.h"

static int _zip_entry_copy(struct zf *dest, struct zf *src,
			  int entry_no, char *name);
static int _zip_entry_add(struct zf *dest, struct zf *src, int entry_no);
static int writecdir(struct zf *zfp)
static void write2(FILE *fp, int i);
static void write4(FILE *fp, int i);
static void writestr(FILE *fp, char *str, int len);
static int writecdentry(FILE *fp, struct zf_entry *zfe, int localp);
static int _zip_create_entry(struct zf *dest, struct zf_entry *src_entry,
			     char *name);



/* zip_close:
   Tries to commit all changes and close the zipfile; if it fails,
   zip_err (and errno) are set and *zf is unchanged, except for
   problems in zf_free. */ 

int
zip_close(struct zf *zf)
{
    int i, count, tfd;
    char *temp;
    FILE *tfp;
    struct zf *tzf;

    if (zf->changes == 0)
	return zf_free(zf);

    /* look if there really are any changes */
    count = 0;
    for (i=0; i<zf->nentry; i++) {
	if (zf->entry[i].state != Z_UNCHANGED) {
	    count = 1;
	    break;
	}
    }

    /* no changes, all has been unchanged */
    if (count == 0)
	return zf_free(zf);
    
    temp = (char *)xmalloc(strlen(zf->zn)+8);
    sprintf(temp, "%s.XXXXXX", zf->zn);

    tfd = mkstemp(temp);

    if ((tfp=fdopen(tfd, "r+b")) == NULL) {
	free(temp);
	close(tfd);
	return -1;
    }

    tzf = zf_new();
    tzf->zp = tfp;
    tzf->zn = temp;
    tzf->nentry = 0;
    tzf->comlen = zf->comlen;
    tzf->cd_size = tzf->cd_offset = 0;
    tzf->com = (unsigned char *)memdup(zf->com, zf->comlen);
    tzf->entry = (struct zf_entry *)xmalloc(sizeof(struct
						   zf_entry)*ALLOC_SIZE);
    tzf->nentry_alloc = ALLOC_SIZE;
    
    count = 0;
    if (zf->entry) {
	for (i=0; i<zf->nentry; i++) {
	    switch (zf->entry[i].state) {
	    case Z_UNCHANGED:
		if (zip_entry_copy(tzf, zf, i, NULL))
		    myerror(ERRFILE, "zip_entry_copy failed");
		break;
	    case Z_DELETED:
		break;
	    case Z_REPLACED:
		/* fallthrough */
	    case Z_ADDED:
		if (zf->entry[i].ch_data_zf) {
		    if (zip_entry_copy(tzf, zf->entry[i].ch_data_zf,
				       zf->entry[i].ch_data_zf_fileno,
				       zf->entry[i].fn))
			myerror(ERRFILE, "zip_entry_copy failed");
		} else if (zf->entry[i].ch_data_buf) {
#if 0
		    zip_entry_add(tzf, zf, i);
#endif /* 0 */
		} else if (zf->entry[i].ch_data_fp) {
#if 0
		    zip_entry_add(tzf, zf, i);
#endif /* 0 */
		} else {
		    /* XXX: ? */
		    myerror(ERRFILE, "Z_ADDED: no data");
		    break;
		}
		break;
	    case Z_RENAMED:
		if (zip_entry_copy(tzf, zf, i, NULL))
		    myerror(ERRFILE, "zip_entry_copy failed");
		break;
	    default:
		/* can't happen */
		break;
	    }
	}
    }

    writecdir(tzf);
    
    if (fclose(tzf->zp)==0) {
	if (rename(tzf->zn, zf->zn) != 0) {
	    zip_err = ZERR_RENAME;
	    zf_free(tzf);
	    return -1;
	}
    }

    free(temp);
    zf_free(zf);

    return 0;
}



static int
_zip_entry_copy(struct zf *dest, struct zf *src, int entry_no, char *name)
{
    char buf[BUFSIZE];
    unsigned int len, remainder;
    unsigned char *null;
    struct zf_entry tempzfe;

    null = NULL;

    zip_create_entry(dest, src->entry+entry_no, name);

    if (fseek(src->zp, src->entry[entry_no].local_offset, SEEK_SET) != 0) {
	zip_err = ZERR_SEEK;
	return -1;
    }

    if (_zip_readcdentry(src->zp, &tempzfe, &null, 0, 1, 1) != 0) {
	zip_err = ZERR_READ;
	return -1;
    }
    
    free(tempzfe.fn);
    tempzfe.fn = xstrdup(dest->entry[dest->nentry-1].fn);
    tempzfe.fnlen = dest->entry[dest->nentry-1].fnlen;

    if (writecdentry(dest->zp, &tempzfe, 1) != 0) {
	zip_err = ZERR_WRITE;
	return -1;
    }
    
    remainder = src->entry[entry_no].comp_size;
    len = BUFSIZE;
    while (remainder) {
	if (len > remainder)
	    len = remainder;
	if (fread(buf, 1, len, src->zp)!=len) {
	    zip_err = ZERR_READ;
	    return -1;
	}
	if (fwrite(buf, 1, len, dest->zp)!=len) {
	    zip_err = ZERR_WRITE;
	    return -1;
	}
	remainder -= len;
    }

    return 0;
}



static int
_zip_entry_add(struct zf *dest, struct zf *src, int entry_no)
{
    z_stream *zstr;
    char *outbuf;
    int ret, wrote;
    
    char buf[BUFSIZE];
    unsigned int len, remainder;
    unsigned char *null;

    null = NULL;

    if (!src)
	return -1;
    
    zip_create_entry(dest, NULL, src->entry[entry_no].fn);

    if (writecdentry(dest->zp, dest->entry+dest->nentry, 1) != 0) {
	zip_err = ZERR_WRITE;
	return -1;
    }

    if (src->entry[entry_no].ch_data_fp) {
	if (fseek(src->entry[entry_no].ch_data_fp,
		  src->entry[entry_no].ch_data_offset, SEEK_SET) != 0) {
	    zip_err = ZERR_SEEK;
	    return -1;
	}
    } else if (src->entry[entry_no].ch_data_buf) {
	if (src->entry[entry_no].ch_data_offset)
	    src->entry[entry_no].ch_data_buf +=
		src->entry[entry_no].ch_data_offset;
	
	if (!src->entry[entry_no].ch_data_len) {
	    /* obviously no data */
	    dest->nentry++;
	    return 0;
	}
    } else 
	return 1;

    zstr->zalloc = Z_NULL;
    zstr->zfree = Z_NULL;
    zstr->opaque = NULL;
    zstr->avail_in = 0;
    zstr->avail_out = 0;

    /* -15: undocumented feature of zlib to _not_ write a zlib header */
    deflateInit2(zstr, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 9,
		 Z_DEFAULT_STRATEGY);

    if (src->entry[entry_no].ch_data_buf) {
	outbuf = (char *)xmalloc(src->entry[entry_no].ch_data_len*1.01+12);
	zstr->next_in = src->entry[entry_no].ch_data_buf;
	zstr->avail_in = src->entry[entry_no].ch_data_len;
	zstr->next_out = outbuf;
	zstr->avail_out = src->entry[entry_no].ch_data_len*1.01+12;

	ret = deflate(zstr, Z_FINISH);

	switch(ret) {
	case Z_STREAM_END:
	    break;
	default:
	    myerror(ERRDEF, "zlib error while deflating buffer: %s",
		    zstr->msg);
	    return -1;
	}
	dest->entry[dest->nentry-1].crc =
	    crc32(dest->entry[dest->nentry-1].crc,
		  src->entry[entry_no].ch_data_buf,
		  src->entry[entry_no].ch_data_len);
	dest->entry[dest->nentry-1].uncomp_size =
	    src->entry[entry_no].ch_data_len;
	dest->entry[dest->nentry-1].comp_size = zstr->total_out;

	wrote = 0;
	if ((ret = fwrite(outbuf+wrote, 1, zstr->total_out-wrote, dest->zp))
	    < zstr->total_out-wrote) {
	    if (ferror(dest->zp)) {
		zip_err = ZERR_WRITE;
		return -1;
	    }
	    wrote += ret;
	}

	fseek(dest->zp, dest->entry[dest->nentry-1].local_offset,
	      SEEK_SET);
	if (ferror(dest->zp)) {
	    zip_err = ZERR_SEEK;
	    return -1;
	}

	if (writecdentry(dest->zp, dest->entry+dest->nentry, 1) != 0) {
	    zip_err = ZERR_WRITE;
	    return -1;
	}

	fseek(dest->zp, 0, SEEK_END);
	if (ferror(dest->zp)) {
	    zip_err = ZERR_SEEK;
	    return -1;
	}
	
	dest->nentry++;
	return 0;
    }

    /* missing: fp, partial zf */
    /* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
#if 0	
	/* XXX: ignore below  */
	remainder = src->entry[entry_no].comp_size;
	len = BUFSIZE;
	while (remainder) {
	    if (len > remainder)
		len = remainder;
	    if (fread(buf, 1, len, src->zp)!=len) {
		zip_err = ZERR_READ;
		return -1;
	    }
	    if (fwrite(buf, 1, len, dest->zp)!=len) {
		zip_err = ZERR_WRITE;
		return -1;
	    }
	    remainder -= len;
	}

	fseek(dest->zp, 0, SEEK_END);
	dest->nentry++;
	return 0;
#endif
	return 1;
}



static int
writecdir(struct zf *zfp)
{
    int i;
    long cd_offset, cd_size;

    cd_offset = ftell(zfp->zp);

    for (i=0; i<zfp->nentry; i++) {
	if (writecdentry(zfp->zp, zfp->entry+i, 0) != 0) {
	    zip_err = ZERR_WRITE;
	    return -1;
	}
    }

    cd_size = ftell(zfp->zp) - cd_offset;
    
    clearerr(zfp->zp);
    fprintf(zfp->zp, EOCD_MAGIC);
    fprintf(zfp->zp, "%c%c%c%c", 0, 0, 0, 0);
    write2(zfp->zp, zfp->nentry);
    write2(zfp->zp, zfp->nentry);
    write4(zfp->zp, cd_size);
    write4(zfp->zp, cd_offset);
    write2(zfp->zp, zfp->comlen);
    writestr(zfp->zp, zfp->com, zfp->comlen);

    return 0;
}



static void
write2(FILE *fp, int i)
{
    putc(i&0xff, fp);
    putc((i>>8)&0xff, fp);

    return;
}



static void
write4(FILE *fp, int i)
{
    putc(i&0xff, fp);
    putc((i>>8)&0xff, fp);
    putc((i>>16)&0xff, fp);
    putc((i>>24)&0xff, fp);
    
    return;
}



static void
writestr(FILE *fp, char *str, int len)
{
#if WIZ
    int i;
    for (i=0; i<len; i++)
	putc(str[i], fp);
#else
    fwrite(str, 1, len, fp);
#endif
    
    return;
}



/* writecdentry:
   if localp, writes local header for zfe to zf->zp,
   else write central directory entry for zfe to zf->zp.
   if after writing ferror(fp), return -1, else return 0.*/
   
static int
writecdentry(FILE *fp, struct zf_entry *zfe, int localp)
{
    fprintf(fp, "%s", localp?LOCAL_MAGIC:CENTRAL_MAGIC);
    
    if (!localp)
	write2(fp, zfe->version_made);
    write2(fp, zfe->version_need);
    write2(fp, zfe->bitflags);
    write2(fp, zfe->comp_meth);
    write2(fp, zfe->lmtime);
    write2(fp, zfe->lmdate);

    write4(fp, zfe->crc);
    write4(fp, zfe->comp_size);
    write4(fp, zfe->uncomp_size);
    
    write2(fp, zfe->fnlen);
    write2(fp, zfe->eflen);
    if (!localp) {
	write2(fp, zfe->fcomlen);
	write2(fp, zfe->disknrstart);
	write2(fp, zfe->intatt);

	write4(fp, zfe->extatt);
	write4(fp, zfe->local_offset);
    }
    
    if (zfe->fnlen)
	writestr(fp, zfe->fn, zfe->fnlen);
    if (zfe->eflen)
	writestr(fp, zfe->ef, zfe->eflen);
    if (zfe->fcomlen)
	writestr(fp, zfe->fcom, zfe->fcomlen);

    if (ferror(fp))
	return -1;
    
    return 0;
}



static int
_zip_create_entry(struct zf *dest, struct zf_entry *src_entry, char *name)
{
    time_t now_t;
    struct tm *now;

    if (!dest)
	return -1;
    
    zip_new_entry(dest);

    if (!src_entry) {
	/* set default values for central directory entry */
	dest->entry[dest->nentry-1].version_made = 20;
	dest->entry[dest->nentry-1].version_need = 20;
	/* maximum compression */
	dest->entry[dest->nentry-1].bitflags = 2;
	/* deflate algorithm */
	dest->entry[dest->nentry-1].comp_meth = 8;
	/* standard MS-DOS format time & date of compression start --
	   thanks Info-Zip! (+1 for rounding) */
	now_t = time(NULL)+1;
	now = localtime(&now_t);
	dest->entry[dest->nentry-1].lmtime = ((now->tm_year+1900-1980)<<9)+
	    ((now->tm_mon+1)<<5) + now->tm_mday;
	dest->entry[dest->nentry-1].lmdate = ((now->tm_hour)<<11)+
	    ((now->tm_min)<<5) + ((now->tm_sec)>>1);
	dest->entry[dest->nentry-1].fcomlen = 0;
	dest->entry[dest->nentry-1].eflen = 0;
	dest->entry[dest->nentry-1].disknrstart = 0;
	/* binary data */
	dest->entry[dest->nentry-1].intatt = 0;
	/* XXX: init CRC-32, compressed and uncompressed size --
	   will be updated later */
	dest->entry[dest->nentry-1].crc = crc32(0, 0, 0);
	dest->entry[dest->nentry-1].comp_size = 0;
	dest->entry[dest->nentry-1].uncomp_size = 0;
	dest->entry[dest->nentry-1].extatt = 0;
	dest->entry[dest->nentry-1].ef = NULL;
	dest->entry[dest->nentry-1].fcom = NULL;
    } else {
	/* copy values from original zf_entry */
	dest->entry[dest->nentry-1].version_made = src_entry->version_made;
	dest->entry[dest->nentry-1].version_need = src_entry->version_need;
	dest->entry[dest->nentry-1].bitflags = src_entry->bitflags;
	dest->entry[dest->nentry-1].comp_meth = src_entry->comp_meth;
	dest->entry[dest->nentry-1].lmtime = src_entry->lmtime;
	dest->entry[dest->nentry-1].lmdate = src_entry->lmdate;
	dest->entry[dest->nentry-1].fcomlen = src_entry->fcomlen;
	dest->entry[dest->nentry-1].eflen = src_entry->eflen;
	dest->entry[dest->nentry-1].disknrstart = src_entry->disknrstart;
	dest->entry[dest->nentry-1].intatt = src_entry->intatt;
	dest->entry[dest->nentry-1].crc = src_entry->crc;
	dest->entry[dest->nentry-1].comp_size = src_entry->comp_size;
	dest->entry[dest->nentry-1].uncomp_size = src_entry->uncomp_size;
	dest->entry[dest->nentry-1].extatt = src_entry->extatt;
	dest->entry[dest->nentry-1].ef = (char *)memdup(src_entry->ef,
						      src_entry->eflen);
	dest->entry[dest->nentry-1].fcom = (char *)memdup(src_entry->fcom,
							src_entry->fcomlen);
    }

    dest->entry[dest->nentry-1].local_offset = ftell(dest->zp);

    if (name) {
	dest->entry[dest->nentry-1].fn = xstrdup(name);
	dest->entry[dest->nentry-1].fnlen = strlen(name);
    } else if (src_entry && src_entry->fn) {
	dest->entry[dest->nentry-1].fn = xstrdup(src_entry->fn);
	dest->entry[dest->nentry-1].fnlen = src_entry->fnlen;
    } else {
	dest->entry[dest->nentry-1].fn = xstrdup("-");
	dest->entry[dest->nentry-1].fnlen = 1;
    }

    return 0;
}
