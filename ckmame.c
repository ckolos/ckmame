#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <getopt.h>

#include "config.h"

#include "types.h"
#include "dbl.h"
#include "funcs.h"
#include "error.h"
#include "r.h"



char *prg;

char *usage = "Usage: %s [-hVnsfbcd] [game...]\n";

char help_head[] = PACKAGE " by Dieter Baron and Thomas Klausner\n\n";

char help[] = "\n\
  -h, --help           display this help message\n\
  -V, --version        display version number\n\
  -n, --nowarnings     print only unfixable errors\n\
  -s, --nosuperfluous  don't report superfluous files\n\
  -f, --nofixable      don't report fixable errors\n\
  -b, --nobroken       don't report unfixable errors\n\
  -d, --nonogooddumps  don't report roms with no good dumps\n\
\n\
Report bugs to <nih@giga.or.at>.\n";

char version_string[] = PACKAGE " " VERSION "\n\
Copyright (C) 1999 Dieter Baron and Thomas Klausner\n\
" PACKAGE " comes with ABSOLUTELY NO WARRANTY, to the extent permitted by law.\n\
You may redistribute copies of\n\
" PACKAGE " under the terms of the GNU General Public License.\n\
For more information about these matters, see the files named COPYING.\n";

#define OPTIONS "hVnsfbcdx"

struct option options[] = {
    { "help",          0, 0, 'h' },
    { "version",       0, 0, 'V' },
    { "nowarnings",    0, 0, 'n' }, /* -SUP, -FIX */
    { "nosuperfluous", 0, 0, 's' }, /* -SUP */
    { "nofixable",     0, 0, 'f' }, /* -FIX */
    { "nobroken",      0, 0, 'b' }, /* -BROKEN */
    { "nonogooddumps", 0, 0, 'd' }, /* -NO_GOOD_DUMPS */
    { "correct",       0, 0, 'c' }, /* +CORRECT */
    { "fix",           0, 0, 'x' },
    { NULL,            0, 0, 0 },
};

int output_options;



int
main(int argc, char **argv)
{
    int i, j;
    DB *db;
    char **list;
    int c, nlist;
    struct tree *tree;
    struct tree tree_root;
    
    prg = argv[0];
    tree = &tree_root;
    tree->child = NULL;
    output_options = WARN_ALL;

    opterr = 0;
    while ((c=getopt_long(argc, argv, OPTIONS, options, 0)) != EOF) {
	switch (c) {
	case 'h':
	    fputs(help_head, stdout);
	    printf(usage, prg);
	    fputs(help, stdout);
	    exit(0);
	case 'V':
	    fputs(version_string, stdout);
	    exit(0);
	case 'x':
	    /* XXX: fix */
	    break;
	case 'n':
	    output_options &= WARN_BROKEN;
	    break;
	case 's':
	    output_options &= ~WARN_SUPERFLUOUS;
	    break;
	case 'f':
	    output_options &= ~WARN_FIXABLE;
	    break;
	case 'b':
	    output_options &= ~WARN_BROKEN;
	    break;
	case 'c':
	    output_options |= WARN_CORRECT;
	    break;
	case 'd':
	    output_options &= ~WARN_NO_GOOD_DUMP;
	    break;
	default:
	    fprintf(stderr, usage, prg);
	    exit(1);
	}
    }
    
    if ((db=db_open("mame", 1, 0))==NULL) {
	myerror(ERRSTR, "can't open database `mame.db'");
	exit(1);
    }

    nlist = r_list(db, "/list", &list);

    if (optind == argc) {
	for (i=0; i<nlist; i++)
	    tree_add(db, tree, list[i]);
    }
    else {
	for (i=optind; i<argc; i++) {
	    if (strcspn(argv[i], "*?[]{}") == strlen(argv[i])) {
		if (bsearch(argv+i, list, nlist, sizeof(char *),
			    strpcasecmp) != NULL)
		    tree_add(db, tree, argv[i]);
	    }
	    else {
		for (j=0; j<nlist; j++) {
		    if (fnmatch(argv[i], list[j], 0) == 0)
			tree_add(db, tree, list[j]);
		}
	    }
	}
    }

    tree_traverse(db, tree);

    return 0;
}
