/*
  memdb.c -- in-memory sqlite3 db
  Copyright (C) 2007-2014 Dieter Baron and Thomas Klausner

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

#include "MemDB.h"

#include "error.h"
#include "sq_util.h"


std::unique_ptr<MemDB> MemDB::memdb;

bool MemDB::inited = false;

#define INSERT_FILE_GAME_ID 1
#define INSERT_FILE_FILE_TYPE 2
#define INSERT_FILE_FILE_IDX 3
#define INSERT_FILE_FILE_SH 4
#define INSERT_FILE_LOCATION 5
#define INSERT_FILE_SIZE 6
#define INSERT_FILE_HASHES 7


bool MemDB::ensure(void) {
    const char *dbname;

    if (inited) {
        return memdb != NULL;
    }

    if (getenv("CKMAME_DEBUG_MEMDB")) {
	dbname = "memdb.sqlite3";
    }
    else {
	dbname = ":memory:";
    }

    inited = true;

    try {
        memdb = std::make_unique<MemDB>(dbname);
    }
    catch (std::exception &e) {
        myerror(ERRSTR, "cannot create in-memory db");
	return false;
    }

    return true;
}


bool MemDB::delete_file(const ArchiveContents *a, size_t idx, bool adjust_idx) {
    if (!ensure()) {
        return false;
    }
    
    if (!memdb->delete_file(a->id, a->filetype, idx)) {
	return false;
    }

    if (!adjust_idx) {
	return true;
    }

    sqlite3_stmt *stmt = memdb->get_statement(DBH_STMT_MEM_DEC_FILE_IDX);

    if (stmt == NULL) {
	return false;
    }

    if (sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(a->id)) != SQLITE_OK || sqlite3_bind_int(stmt, 2, a->filetype) != SQLITE_OK || sqlite3_bind_int(stmt, 3, static_cast<int>(idx)) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) {
        return false;
    }

    return true;
}


bool MemDB::insert_file(sqlite3_stmt *stmt, const ArchiveContents *a, size_t idx) {
    if (!ensure()) {
	return false;
    }

    auto r = &a->files[idx];

    if (stmt == NULL) {
        if ((stmt = memdb->get_statement(DBH_STMT_MEM_INSERT_FILE)) == NULL) {
	    return false;
        }

        if (sq3_set_uint64(stmt, INSERT_FILE_GAME_ID, a->id) != SQLITE_OK || sqlite3_bind_int(stmt, INSERT_FILE_FILE_TYPE, a->filetype) != SQLITE_OK || sqlite3_bind_int(stmt, INSERT_FILE_LOCATION, a->where) != SQLITE_OK) {
	    return false;
        }
    }

    auto ok = true;

    if (sqlite3_bind_int(stmt, INSERT_FILE_FILE_IDX, static_cast<int>(idx)) != SQLITE_OK) {
        return false;
    }
    
    for (size_t i = 0; i < 2; i++) {
        bool detector = (i == 1);
        if (detector && !r->size_hashes_are_set(detector)) {
            continue;
        }
        
        if (sqlite3_bind_int(stmt, INSERT_FILE_FILE_SH, static_cast<int>(i)) != SQLITE_OK || sq3_set_uint64_default(stmt, INSERT_FILE_SIZE, r->get_size(detector), SIZE_UNKNOWN) != SQLITE_OK || sq3_set_hashes(stmt, INSERT_FILE_HASHES, &r->get_hashes(detector), 1) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE || sqlite3_reset(stmt) != SQLITE_OK) {
            ok = false;
            continue;
        }
    }

    return ok;
}


bool MemDB::insert_archive(const ArchiveContents *archive) {
    if (!ensure()) {
        return false;
    }

    auto stmt = memdb->get_statement(DBH_STMT_MEM_INSERT_FILE);
    if (stmt == NULL) {
        return false;
    }

    if (sq3_set_uint64(stmt, INSERT_FILE_GAME_ID, archive->id) != SQLITE_OK || sqlite3_bind_int(stmt, INSERT_FILE_FILE_TYPE, archive->filetype) != SQLITE_OK || sqlite3_bind_int(stmt, INSERT_FILE_LOCATION, archive->where) != SQLITE_OK) {
        return false;
    }

    auto ok = true;
    
    for (size_t i = 0; i < archive->files.size(); i++) {
        if (archive->files[i].status != STATUS_OK) {
	    continue;
        }
        if (!insert_file(stmt, archive, i)) {
            ok = false;
        }
    }

    return ok;
}


bool MemDB::update_file(const ArchiveContents *archive, size_t idx) {
    if (!ensure()) {
        return false;
    }
    
    if (archive->files[idx].status != STATUS_OK) {
	return memdb->delete_file(archive->id, archive->filetype, idx);
    }

    return memdb->update_file(archive->id, archive->filetype, idx, &archive->files[idx].hashes);
}


bool MemDB::update_file(uint64_t id, filetype_t ft, size_t idx, const Hashes *h) {
    /* FILE_SH_DETECTOR hashes are always completely filled in */

    sqlite3_stmt *stmt = get_statement(DBH_STMT_MEM_UPDATE_FILE);
    if (stmt == NULL) {
	return false;
    }

    if (sq3_set_hashes(stmt, 1, h, 1) != SQLITE_OK || sq3_set_uint64(stmt, 4, id) != SQLITE_OK || sqlite3_bind_int(stmt, 5, ft) != SQLITE_OK || sq3_set_uint64(stmt, 6, idx) != SQLITE_OK || sqlite3_bind_int(stmt, 7, 0) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) {
	return false;
    }

    return true;
}


std::optional<std::vector<MemDB::FindResult>> MemDB::find(filetype_t filetype, const File *file) {
    if (!ensure()) {
        return {};
    }
    
    auto stmt = memdb->get_statement(DBH_STMT_MEM_QUERY_FILE, &file->hashes, file->size != SIZE_UNKNOWN);
    if (stmt == NULL) {
        return {};
    }
    
    int hcol = 2;
    if (file->size != SIZE_UNKNOWN) {
        hcol++;
        if (sq3_set_uint64(stmt, 2, file->size) != SQLITE_OK) {
            return {};
        }
    }
    
    if (sqlite3_bind_int(stmt, 1, filetype) != SQLITE_OK || sq3_set_hashes(stmt, hcol, &file->hashes, 0) != SQLITE_OK) {
        return {};
    }

    std::vector<FindResult> results;
    
    int ret;
    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
        FindResult result;
        
        result.game_id = sq3_get_uint64(stmt, 0);
        result.index = sq3_get_uint64(stmt, 1);
        result.sh = sqlite3_column_int(stmt, 2);
        result.location = static_cast<where_t>(sqlite3_column_int(stmt, 3));
        
        results.push_back(result);
    }
    
    if (ret != SQLITE_DONE) {
        return {};
    }
    
    return results;
}


bool MemDB::delete_file(uint64_t id, filetype_t filetype, size_t index) {
    sqlite3_stmt *stmt = get_statement(DBH_STMT_MEM_DELETE_FILE);
    if (stmt == NULL) {
	return false;
    }

    if (sq3_set_uint64(stmt, 1, id) != SQLITE_OK || sqlite3_bind_int(stmt, 2, filetype) != SQLITE_OK || sq3_set_uint64(stmt, 3, index) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) {
	return false;
    }

    return true;
}
