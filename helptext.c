/* Help text and version information
 * This file is part of jdupes; see jdupes.c for license information */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include <libjodycode.h>
#include "filehash.h"
#include "helptext.h"
#include "jdupes.h"
#include "version.h"


#ifndef NO_HELPTEXT
/* Assemble feature flag string from compile-time options */
const char *feature_flags[] = {
  #ifdef ENABLE_DEDUPE
  "dedupe",
  #endif
  #ifdef DEBUG
  "debug",
  #endif
  #ifdef __FAST_MATH__
  "fastmath",
  #endif
  #ifdef LOUD_DEBUG
  "loud",
  #endif
  #ifdef LOW_MEMORY
  "lowmem",
  #endif
  #ifdef NO_CHUNKSIZE
  "nochunk",
  #endif
  #ifdef NO_DELETE
  "nodel",
  #endif
  #ifdef NO_ERRORONDUPE
  "noeod",
  #endif
  #ifdef NO_EXTFILTER
  "noxf",
  #endif
  #ifdef NO_HARDLINKS
  "nohlink",
  #endif
  #ifdef NO_HASHDB
  "nohashdb",
  #endif
  #ifdef NO_NUMSORT
  "nojsort",
  #endif
  #ifdef NO_JSON
  "nojson",
  #endif
  #ifdef NO_GETOPT_LONG
  "nolongopt",
  #endif
  #ifdef NO_MTIME
  "nomtime",
  #endif
  #ifdef NO_PERMS
  "noperm",
  #endif
  #ifdef NO_SYMLINKS
  "noslink",
  #endif
  #ifdef NO_TRAVCHECK
  "notrav",
  #endif
  #ifdef NO_USER_ORDER
  "nouorder",
  #endif
  #ifdef NO_UNICODE
  "nounicode",
  #endif
  #ifdef UNICODE
  "unicode",
  #endif
  #ifdef ON_WINDOWS
  "windows",
  #endif
  NULL
};
#endif /* NO_HELPTEXT */


void help_text(void)
{
#ifndef NO_HELPTEXT
  printf("Usage: jdupes [options] FILES and/or DIRECTORIES...\n\n");

  printf("Duplicate file sets will be printed by default unless a different action\n");
  printf("option is specified (delete, summarize, link, dedupe, etc.)\n");
#ifdef NO_GETOPT_LONG
  printf("\nWARNING: getopt_long disabled in this build! Long options will not work.\n\n");
#endif
#ifdef LOUD
  printf(" -@ --loud        \toutput annoying low-level debug info while running\n");
#endif
  printf(" -0 --print-null  \toutput nulls instead of CR/LF (like 'find -print0')\n");
  printf(" -1 --one-file-system\tdo not match files on different filesystems/devices\n");
  printf(" -A --no-hidden    \texclude hidden files from consideration\n");
#ifdef ENABLE_DEDUPE
  printf(" -B --dedupe      \tdo a copy-on-write (reflink/clone) deduplication\n");
#endif
#ifndef NO_CHUNKSIZE
  printf(" -C --chunk-size=#\toverride I/O chunk size in KiB (min %d, max %d)\n", MIN_CHUNK_SIZE / 1024, MAX_CHUNK_SIZE / 1024);
#endif /* NO_CHUNKSIZE */
#ifndef NO_DELETE
  printf(" -d --delete      \tprompt user for files to preserve and delete all\n");
  printf("                  \tothers; important: under particular circumstances,\n");
  printf("                  \tdata may be lost when using this option together\n");
  printf("                  \twith -s or --symlinks, or when specifying a\n");
  printf("                  \tparticular directory more than once; refer to the\n");
  printf("                  \tdocumentation for additional information\n");
#endif /* NO_DELETE */
#ifdef DEBUG
  printf(" -D --debug       \toutput debug statistics after completion\n");
#endif
#ifndef NO_ERRORONDUPE
  printf(" -e --error-on-dupe\texit on any duplicate found with status code 255\n");
#endif
  printf(" -f --omit-first  \tomit the first file in each set of matches\n");
  printf(" -h --help        \tdisplay this help message\n");
#ifndef NO_HARDLINKS
  printf(" -H --hard-links  \ttreat any linked files as duplicate files. Normally\n");
  printf("                  \tlinked files are treated as non-duplicates for safety\n");
#endif
  printf(" -i --reverse     \treverse (invert) the match sort order\n");
#ifndef NO_USER_ORDER
  printf(" -I --isolate     \tfiles in the same specified directory won't match\n");
#endif
#ifndef NO_JSON
  printf(" -j --json        \tproduce JSON (machine-readable) output\n");
#endif /* NO_JSON */
/*  printf(" -K --skip-hash   \tskip full file hashing (may be faster; 100%% safe)\n");
    printf("                  \tWARNING: in development, not fully working yet!\n"); */
#ifndef NO_SYMLINKS
  printf(" -l --link-soft    \tmake relative symlinks for duplicates w/o prompting\n");
#endif
#ifndef NO_HARDLINKS
  printf(" -L --link-hard    \thard link all duplicate files without prompting\n");
 #ifdef ON_WINDOWS
  printf("                  \tWindows allows a maximum of 1023 hard links per file;\n");
  printf("                  \tlinking large match sets will result in multiple sets\n");
  printf("                  \tof hard linked files due to this limit.\n");
 #endif /* ON_WINDOWS */
#endif /* NO_HARDLINKS */
  printf(" -m --summarize   \tsummarize dupe information\n");
  printf(" -M --print-summarize\tprint match sets and --summarize at the end\n");
#ifndef NO_DELETE
  printf(" -N --no-prompt   \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
#endif /* NO_DELETE */
#ifndef NO_MTIME
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
  printf("                  \tmtime (BY=time) or filename (BY=name, the default)\n");
#endif
#ifndef NO_USER_ORDER
  printf(" -O --param-order  \tParameter order is more important than selected -o sort\n");
#endif
#ifndef NO_PERMS
  printf(" -p --permissions \tdon't consider files with different owner/group or\n");
  printf("                  \tpermission bits as duplicates\n");
#endif
  printf(" -P --print=type  \tprint extra info (partial, early, fullhash)\n");
  printf(" -q --quiet       \thide progress indicator\n");
  printf(" -Q --quick       \tskip byte-for-byte confirmation for quick matching\n");
  printf("                  \tWARNING: -Q can result in data loss! Be very careful!\n");
  printf(" -r --recurse     \tfor every directory, process its subdirectories too\n");
  printf(" -R --recurse:    \tfor each directory given after this option follow\n");
  printf("                  \tsubdirectories encountered within (note the ':' at\n");
  printf("                  \tthe end of the option, manpage for more details)\n");
#ifndef NO_SYMLINKS
  printf(" -s --symlinks    \tfollow symlinks\n");
#endif
  printf(" -S --size        \tshow size of duplicate files\n");
  printf(" -t --no-change-check\tdisable security check for file changes (aka TOCTTOU)\n");
  printf(" -T --partial-only \tmatch based on partial hashes only. WARNING:\n");
  printf("                  \tEXTREMELY DANGEROUS paired with destructive actions!\n");
  printf(" -u --print-unique\tprint only a list of unique (non-matched) files\n");
  printf(" -U --no-trav-check\tdisable double-traversal safety check (BE VERY CAREFUL)\n");
  printf("                  \tThis fixes a Google Drive File Stream recursion issue\n");
  printf(" -v --version     \tdisplay jdupes version and license information\n");
#ifndef NO_EXTFILTER
  printf(" -X --ext-filter=x:y\tfilter files based on specified criteria\n");
  printf("                  \tUse '-X help' for detailed extfilter help\n");
#endif /* NO_EXTFILTER */
  printf(" -y --hash-db=file\tuse a hash database text file to speed up repeat runs\n");
  printf("                  \tPassing '-y .' will expand to  '-y jdupes_hashdb.txt'\n");
  printf(" -z --zero-match  \tconsider zero-length files to be duplicates\n");
  printf(" -Z --soft-abort  \tIf the user aborts (i.e. CTRL-C) act on matches so far\n");
#ifndef ON_WINDOWS
  printf("                  \tYou can send SIGUSR1 to the program to toggle this\n");
#endif

#else /* NO_HELPTEXT */
  version_text(0);
#endif /* NO_HELPTEXT */
  return;
}


void version_text(int short_version)
{
  printf("jdupes %s (%s) ", VER, VERDATE);

#ifndef NO_HELPTEXT
  /* Indicate bitness information */
  if (sizeof(uintptr_t) == 8) {
    if (sizeof(long) == 4) printf("64-bit i32");
    else if (sizeof(long) == 8) printf("64-bit");
  } else if (sizeof(uintptr_t) == 4) {
    if (sizeof(long) == 4) printf("32-bit");
    else if (sizeof(long) == 8) printf("32-bit i64");
#if defined(__x86_64__) && SIZE_MAX == 0xffffffff
	printf(" (x32 ABI)");
#endif
  } else printf("%u-bit i%u", (unsigned int)(sizeof(uintptr_t) * 8),
      (unsigned int)(sizeof(long) * 8));
  if (!short_version) {
    printf(", linked to libjodycode %s (%s)\n", jc_version, jc_verdate);
    printf("Hash algorithms available:");
    for (int i = 0; i < HASH_ALGO_COUNT; i++) printf(" %s%c", hash_algo_list[i], i == (HASH_ALGO_COUNT - 1) ? '\n' : ',');
  } else printf("\n");

  printf("Compile-time feature flags:");
  if (*feature_flags != NULL) {
    int c = 0;
    while (feature_flags[c] != NULL) {
      printf(" %s", feature_flags[c]);
      c++;
    }
  } else printf(" none");
  printf("\n");
  if (short_version) return;
  printf("Copyright (C) 2015-2023 by Jody Bruchon and contributors\n\n");
  printf("Permission is hereby granted, free of charge, to any person obtaining a copy of\n");
  printf("this software and associated documentation files (the \"Software\"), to deal in\n");
  printf("the Software without restriction, including without limitation the rights to\n");
  printf("use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies\n");
  printf("of the Software, and to permit persons to whom the Software is furnished to do\n");
  printf("so, subject to the following conditions:\n\n");

  printf("The above copyright notice and this permission notice shall be included in all\n");
  printf("copies or substantial portions of the Software.\n\n");
  printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n");
  printf("IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n");
  printf("FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n");
  printf("AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n");
  printf("LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n");
  printf("OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n");
  printf("SOFTWARE.\n");
  printf("\nIf you find this software useful, please consider financially supporting\n");
  printf("its development through the author's home page: https://www.jodybruchon.com/\n");
  printf("Report bugs, get new releases, or learn about jdupes: https://www.jdupes.com/\n");
#else
  (void)short_version;
  printf("\nBuilt with no help text. You're on your own.\n");
#endif /* NO_HELPTEXT */
  return;
}
