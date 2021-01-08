/*
  tree.c -- traverse tree of games to check
  Copyright (C) 1999-2015 Dieter Baron and Thomas Klausner

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

#include "archive.h"
#include "dbh.h"
#include "error.h"
#include "funcs.h"
#include "game.h"
#include "globals.h"
#include "sighandle.h"
#include "tree.h"
#include "xmalloc.h"

static tree_t *tree_add_node(tree_t *, const char *, int);
static tree_t *tree_new_full(const char *, int);
static int tree_process(tree_t *, ArchivePtr [], ImagesPtr []);
static void tree_traverse_internal(tree_t *, ArchivePtr [], ImagesPtr []);


int
tree_add(tree_t *tree, const char *name) {
    GamePtr game = romdb_read_game(db, name);
    
    if (!game) {
	return -1;
    }

    if (!game->cloneof[1].empty()) {
	tree = tree_add_node(tree, game->cloneof[1].c_str(), 0);
    }
    if (!game->cloneof[0].empty()) {
        tree = tree_add_node(tree, game->cloneof[0].c_str(), 0);
    }

    tree_add_node(tree, name, 1);

    return 0;
}


void
tree_free(tree_t *tree) {
    tree_t *t;

    while (tree) {
	if (tree->child)
	    tree_free(tree->child);
	t = tree;
	tree = tree->next;
	free(t->name);
	free(t);
    }
}


tree_t *
tree_new(void) {
    tree_t *t;

    t = static_cast<tree_t *>(xmalloc(sizeof(*t)));

    t->name = NULL;
    t->check = t->checked = false;
    t->child = t->next = NULL;

    return t;
}


bool
tree_recheck(const tree_t *tree, const char *name) {
    tree_t *t;

    for (t = tree->child; t; t = t->next) {
	if (strcmp(tree_name(t), name) == 0) {
	    tree_checked(t) = false;
	    return tree_check(t);
	}
	if (tree_recheck(t, name)) {
	    return true;
	}
    }

    return false;
}


bool
tree_recheck_games_needing(tree_t *tree, uint64_t size, const Hashes *hashes) {
    GamePtr game;
    const File *gr;
    
    auto roms = romdb_read_file_by_hash(db, TYPE_ROM, hashes);
    if (roms.empty()) {
	return 0;
    }

    auto ret = true;
    for (size_t i = 0; i < roms.size(); i++) {
	auto rom = roms[i];

        game = romdb_read_game(db, rom.name);
        if (!game || game->roms.size() <= rom.index) {
            /* TODO: internal error: db inconsistency */
	    ret = false;
	    continue;
	}

        gr = &game->roms[rom.index];

	if (size == gr->size && hashes->compare(gr->hashes) == Hashes::MATCH && gr->where == FILE_INGAME)
	    tree_recheck(tree, game->name.c_str());
    }

    return ret;
}


void
tree_traverse(tree_t *tree) {
    ArchivePtr archives[] = { NULL, NULL, NULL };
    ImagesPtr images[] = { std::make_shared<Images>(), std::make_shared<Images>(), std::make_shared<Images>() };
    tree_traverse_internal(tree, archives, images);
}

static void
tree_traverse_internal(tree_t *tree, ArchivePtr ancestor_archives[], ImagesPtr ancestor_images[]) {
    tree_t *t;
    int flags;

    ArchivePtr archives[] = { NULL, ancestor_archives[0], ancestor_archives[1] };
    ImagesPtr images[] = { std::make_shared<Images>(), ancestor_images[0], ancestor_images[1] };
    
    ImagesPtr self_images;
    
    if (tree->name) {
	if (siginfo_caught)
	    print_info(tree->name);

	flags = ((tree->check ? ARCHIVE_FL_CREATE : 0) | (check_integrity ? (ARCHIVE_FL_CHECK_INTEGRITY | romdb_hashtypes(db, TYPE_ROM)) : 0));

	auto full_name = findfile(tree->name, TYPE_ROM, "");
	if (full_name == "" && tree->check) {
	    full_name = make_file_name(TYPE_ROM, tree->name, "");
	}
	if (full_name != "")
	    archives[0] = Archive::open(full_name, TYPE_ROM, FILE_ROMSET, flags);
        
        self_images = Images::from_directory(tree->name, check_integrity);
        images[0] = self_images;
        
	if (tree_check(tree) && !tree_checked(tree))
            tree_process(tree, archives, images);
    }

    for (t = tree->child; t; t = t->next) {
	tree_traverse_internal(t, archives, images);
    }

    return;
}


static tree_t *
tree_add_node(tree_t *tree, const char *name, int check) {
    tree_t *t;
    int cmp;

    if (tree->child == NULL) {
	t = tree_new_full(name, check);
	tree->child = t;
	return t;
    }
    else {
	cmp = strcmp(tree->child->name, name);
	if (cmp == 0) {
	    if (check)
		tree->child->check = 1;
	    return tree->child;
	}
	else if (cmp > 0) {
	    t = tree_new_full(name, check);
	    t->next = tree->child;
	    tree->child = t;
	    return t;
	}
	else {
	    for (tree = tree->child; tree->next; tree = tree->next) {
		cmp = strcmp(tree->next->name, name);
		if (cmp == 0) {
		    if (check)
			tree->next->check = 1;
		    return tree->next;
		}
		else if (cmp > 0) {
		    t = tree_new_full(name, check);
		    t->next = tree->next;
		    tree->next = t;
		    return t;
		}
	    }

	    t = tree_new_full(name, check);
	    tree->next = t;
	    return t;
	}
    }
}


static tree_t *
tree_new_full(const char *name, int check) {
    tree_t *t;

    t = tree_new();
    t->name = xstrdup(name);
    t->check = check;

    return t;
}


static int
tree_process(tree_t *tree, ArchivePtr archives[], ImagesPtr images[]) {
    GamePtr game;

    /* check me */
    if (!(game = romdb_read_game(db, tree->name))) {
	myerror(ERRDEF, "db error: %s not found", tree->name);
	return -1;
    }

    Result res(game.get(), archives[0].get(), images[0].get());

    check_old(game.get(), &res);
    check_files(game.get(), archives, &res);
    check_archive(archives[0], game->name.c_str(), &res);
    check_disks(game.get(), images, &res);
    check_images(images[0].get(), game->name.c_str(), &res);

    /* write warnings/errors for me */
    diagnostics(game.get(), archives[0], images[0].get(), &res);

    int ret = 0;

    if (fix_options & FIX_DO) {
	ret = fix_game(game.get(), archives[0].get(), images[0].get(), &res);
    }

    /* TODO: includes too much when rechecking */
    if (fixdat) {
	write_fixdat_entry(game.get(), &res);
    }

    if (ret != 1) {
	tree_checked(tree) = true;
    }

    return 0;
}
