/* jdupes (C) 2015-2023 Jody Bruchon <jody@jodybruchon.com>

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation files
   (the "Software"), to deal in the Software without restriction,
   including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software,
   and to permit persons to whom the Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#ifndef NO_GETOPT_LONG
 #include <getopt.h>
#endif
#include <errno.h>

#include <libjodycode.h>
#include "libjodycode_check.h"

#include "likely_unlikely.h"
#include "jdupes.h"
#include "args.h"
#include "checks.h"
#ifndef NO_EXTFILTER
 #include "extfilter.h"
#endif
#include "filehash.h"
#include "filestat.h"
#include "helptext.h"
#include "loaddir.h"
#include "progress.h"
#include "interrupt.h"
#include "sort.h"
#ifndef NO_TRAVCHECK
 #include "travcheck.h"
#endif
#include "version.h"

#ifndef USE_JODY_HASH
 #include "xxhash.h"
#endif
#ifdef ENABLE_DEDUPE
 #ifdef __linux__
  #include <sys/utsname.h>
 #endif
#endif

/* Headers for post-scanning actions */
#include "act_deletefiles.h"
#ifdef ENABLE_DEDUPE
 #include "act_dedupefiles.h"
#endif
#include "act_linkfiles.h"
#include "act_printmatches.h"
#ifndef NO_JSON
 #include "act_printjson.h"
#endif /* NO_JSON */
#include "act_summarize.h"


/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __MINGW32__
 #ifdef UNICODE
  const wchar_t *FILE_MODE_RO = L"rbS";
  wpath_t wstr;
 #else
  const char *FILE_MODE_RO = "rbS";
 #endif /* UNICODE */
#else /* Not Windows */
 const char *FILE_MODE_RO = "rb";
 #ifdef UNICODE
  #error Do not define UNICODE on non-Windows platforms.
  #undef UNICODE
 #endif
#endif /* _WIN32 || __MINGW32__ */

/* Behavior modification flags (a=action, p=-P) */
uint_fast64_t flags = 0, a_flags = 0, p_flags = 0;

static const char *program_name;

/* Stat and SIGUSR */
#ifdef ON_WINDOWS
 struct jc_winstat s;
#else
 struct stat s;
#endif

#ifndef PARTIAL_HASH_SIZE
 #define PARTIAL_HASH_SIZE 4096
#endif

#ifndef NO_CHUNKSIZE
 size_t auto_chunk_size = CHUNK_SIZE;
#else
 /* If automatic chunk sizing is disabled, just use a fixed value */
 #define auto_chunk_size CHUNK_SIZE
#endif /* NO_CHUNKSIZE */

/* Required for progress indicator code */
uintmax_t filecount = 0, progress = 0, item_progress = 0, dupecount = 0;

/* Performance and behavioral statistics (debug mode) */
#ifdef DEBUG
unsigned int small_file = 0, partial_hash = 0, partial_elim = 0;
unsigned int full_hash = 0, partial_to_full = 0, hash_fail = 0;
uintmax_t comparisons = 0;
 #ifdef ON_WINDOWS
  #ifndef NO_HARDLINKS
  unsigned int hll_exclude = 0;
  #endif
 #endif
#endif /* DEBUG */

/* File tree head */
static filetree_t *checktree = NULL;

/* Directory/file parameter position counter */
unsigned int user_item_count = 1;

/* registerfile() direction options */
enum tree_direction { NONE, LEFT, RIGHT };

/* Sort order reversal */
int sort_direction = 1;

/* For path name mangling */
char tempname[PATHBUF_SIZE * 2];

/* Strings used in multiple places */
const char *s_interrupt = "\nStopping file scan due to user abort\n";
const char *s_no_dupes = "No duplicates found.\n";

/***** End definitions, begin code *****/

/***** Add new functions here *****/


static inline void registerfile(filetree_t * restrict * const restrict nodeptr,
                const enum tree_direction d, file_t * const restrict file)
{
  filetree_t * restrict branch;

  if (unlikely(nodeptr == NULL || file == NULL || (d != NONE && *nodeptr == NULL))) jc_nullptr("registerfile()");
  LOUD(fprintf(stderr, "registerfile(direction %d)\n", d));

  /* Allocate and initialize a new node for the file */
  branch = (filetree_t *)malloc(sizeof(filetree_t));
  if (unlikely(branch == NULL)) jc_oom("registerfile() branch");
  branch->file = file;
  branch->left = NULL;
  branch->right = NULL;

  /* Attach the new node to the requested branch */
  switch (d) {
    case LEFT:
      (*nodeptr)->left = branch;
      break;
    case RIGHT:
      (*nodeptr)->right = branch;
      break;
    case NONE:
      /* For the root of the tree only */
      *nodeptr = branch;
      break;
    default:
      /* This should never ever happen */
      fprintf(stderr, "\ninternal error: invalid direction for registerfile(), report this\n");
      exit(EXIT_FAILURE);
      break;
  }

  return;
}


/* Check two files for a match */
static file_t **checkmatch(filetree_t * restrict tree, file_t * const restrict file)
{
  int cmpresult = 0;
  int cantmatch = 0;
  const jdupes_hash_t * restrict filehash;

  if (unlikely(tree == NULL || file == NULL || tree->file == NULL || tree->file->d_name == NULL || file->d_name == NULL)) jc_nullptr("checkmatch()");
  LOUD(fprintf(stderr, "checkmatch ('%s', '%s')\n", tree->file->d_name, file->d_name));

  /* If device and inode fields are equal one of the files is a
   * hard link to the other or the files have been listed twice
   * unintentionally. We don't want to flag these files as
   * duplicates unless the user specifies otherwise. */

  /* Count the total number of comparisons requested */
  DBG(comparisons++;)

/* If considering hard linked files as duplicates, they are
 * automatically duplicates without being read further since
 * they point to the exact same inode. If we aren't considering
 * hard links as duplicates, we just return NULL. */

  cmpresult = check_conditions(tree->file, file);
  switch (cmpresult) {
    case 2: return &tree->file;  /* linked files + -H switch */
    case -2: return NULL;  /* linked files, no -H switch */
    case -3:    /* user order */
    case -4:    /* one filesystem */
    case -5:    /* permissions */
        cantmatch = 1;
        cmpresult = 0;
        break;
    default: break;
  }

  /* Print pre-check (early) match candidates if requested */
  if (ISFLAG(p_flags, PF_EARLYMATCH)) printf("Early match check passed:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);

  /* If preliminary matching succeeded, do main file data checks */
  if (cmpresult == 0) {
    LOUD(fprintf(stderr, "checkmatch: starting file data comparisons\n"));
    /* Attempt to exclude files quickly with partial file hashing */
    if (!ISFLAG(tree->file->flags, FF_HASH_PARTIAL)) {
      filehash = get_filehash(tree->file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) return NULL;

      tree->file->filehash_partial = *filehash;
      SETFLAG(tree->file->flags, FF_HASH_PARTIAL);
    }

    if (!ISFLAG(file->flags, FF_HASH_PARTIAL)) {
      filehash = get_filehash(file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) return NULL;

      file->filehash_partial = *filehash;
      SETFLAG(file->flags, FF_HASH_PARTIAL);
    }

    cmpresult = HASH_COMPARE(file->filehash_partial, tree->file->filehash_partial);
    LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: partial hashes match\n"));
    LOUD(if (cmpresult) fprintf(stderr, "checkmatch: partial hashes do not match\n"));
    DBG(partial_hash++;)

    /* Print partial hash matching pairs if requested */
    if (cmpresult == 0 && ISFLAG(p_flags, PF_PARTIAL))
      printf("\nPartial hashes match:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);

    if (file->size <= PARTIAL_HASH_SIZE || ISFLAG(flags, F_PARTIALONLY)) {
      if (ISFLAG(flags, F_PARTIALONLY)) { LOUD(fprintf(stderr, "checkmatch: partial only mode: treating partial hash as full hash\n")); }
      else { LOUD(fprintf(stderr, "checkmatch: small file: copying partial hash to full hash\n")); }
      /* filehash_partial = filehash if file is small enough */
      if (!ISFLAG(file->flags, FF_HASH_FULL)) {
        file->filehash = file->filehash_partial;
        SETFLAG(file->flags, FF_HASH_FULL);
        DBG(small_file++;)
      }
      if (!ISFLAG(tree->file->flags, FF_HASH_FULL)) {
        tree->file->filehash = tree->file->filehash_partial;
        SETFLAG(tree->file->flags, FF_HASH_FULL);
        DBG(small_file++;)
      }
    } else if (cmpresult == 0) {
//      if (ISFLAG(flags, F_SKIPHASH)) {
//        LOUD(fprintf(stderr, "checkmatch: skipping full file hashes (F_SKIPMATCH)\n"));
//      } else {
        /* If partial match was correct, perform a full file hash match */
        if (!ISFLAG(tree->file->flags, FF_HASH_FULL)) {
          filehash = get_filehash(tree->file, 0);
          if (filehash == NULL) return NULL;

          tree->file->filehash = *filehash;
          SETFLAG(tree->file->flags, FF_HASH_FULL);
        }

        if (!ISFLAG(file->flags, FF_HASH_FULL)) {
          filehash = get_filehash(file, 0);
          if (filehash == NULL) return NULL;

          file->filehash = *filehash;
          SETFLAG(file->flags, FF_HASH_FULL);
        }

        /* Full file hash comparison */
        cmpresult = HASH_COMPARE(file->filehash, tree->file->filehash);
        LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: full hashes match\n"));
        LOUD(if (cmpresult) fprintf(stderr, "checkmatch: full hashes do not match\n"));
        DBG(full_hash++);
//      }
    } else {
      DBG(partial_elim++);
    }
  }  /* if (cmpresult == 0) */

  if ((cantmatch != 0) && (cmpresult == 0)) {
    LOUD(fprintf(stderr, "checkmatch: rejecting because match not allowed (cantmatch = 1)\n"));
    cmpresult = -1;
  }

  /* How the file tree works
   *
   * The tree is sorted by size as files arrive. If the files are the same
   * size, they are possible duplicates and are checked for duplication.
   * If they are not a match, the hashes are used to decide whether to
   * continue with the file to the left or the right in the file tree.
   * If the direction decision points to a leaf node, the duplicate scan
   * continues down that path; if it points to an empty node, the current
   * file is attached to the file tree at that point.
   *
   * This allows for quickly finding files of the same size by avoiding
   * tree branches with differing size groups.
   */
  if (cmpresult < 0) {
    if (tree->left != NULL) {
      LOUD(fprintf(stderr, "checkmatch: recursing tree: left\n"));
      return checkmatch(tree->left, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: left\n"));
      registerfile(&tree, LEFT, file);
      return NULL;
    }
  } else if (cmpresult > 0) {
    if (tree->right != NULL) {
      LOUD(fprintf(stderr, "checkmatch: recursing tree: right\n"));
      return checkmatch(tree->right, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: right\n"));
      registerfile(&tree, RIGHT, file);
      return NULL;
    }
  } else {
    /* All compares matched */
    DBG(partial_to_full++;)
    LOUD(fprintf(stderr, "checkmatch: files appear to match based on hashes\n"));
    if (ISFLAG(p_flags, PF_FULLHASH)) printf("Full hashes match:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);
    return &tree->file;
  }
  /* Fall through - should never be reached */
  return NULL;
}


/* Do a byte-by-byte comparison in case two different files produce the
   same signature. Unlikely, but better safe than sorry. */
static inline int confirmmatch(FILE * const restrict file1, FILE * const restrict file2, const off_t size)
{
  static char *c1 = NULL, *c2 = NULL;
  size_t r1, r2;
  off_t bytes = 0;

  if (unlikely(file1 == NULL || file2 == NULL)) jc_nullptr("confirmmatch()");
  LOUD(fprintf(stderr, "confirmmatch running\n"));

  /* Allocate on first use; OOM if either is ever NULLed */
  if (!c1) {
    c1 = (char *)malloc(auto_chunk_size);
    c2 = (char *)malloc(auto_chunk_size);
  }
  if (unlikely(!c1 || !c2)) jc_oom("confirmmatch() c1/c2");

  fseek(file1, 0, SEEK_SET);
  fseek(file2, 0, SEEK_SET);

  do {
    if (interrupt) return 0;
    r1 = fread(c1, sizeof(char), auto_chunk_size, file1);
    r2 = fread(c2, sizeof(char), auto_chunk_size, file2);

    if (r1 != r2) return 0; /* file lengths are different */
    if (memcmp (c1, c2, r1)) return 0; /* file contents are different */

    bytes += (off_t)r1;
    if (jc_alarm_ring != 0) {
      jc_alarm_ring = 0;
      update_phase2_progress("confirm", (int)((bytes * 100) / size));
    }
  } while (r2);

  return 1;
}


/* Count the following statistics:
   - Maximum number of files in a duplicate set (length of longest dupe chain)
   - Number of non-zero-length files that have duplicates (if n_files != NULL)
   - Total number of duplicate file sets (groups) */
unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max,
                unsigned int * const restrict n_files) {
  unsigned int groups = 0;

  if (unlikely(files == NULL || max == NULL)) jc_nullptr("get_max_dupes()");
  LOUD(fprintf(stderr, "get_max_dupes(%p, %p, %p)\n", (const void *)files, (void *)max, (void *)n_files));

  *max = 0;
  if (n_files) *n_files = 0;

  while (files) {
    unsigned int n_dupes;
    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
      groups++;
      if (n_files && files->size) (*n_files)++;
      n_dupes = 1;
      for (file_t *curdupe = files->duplicates; curdupe; curdupe = curdupe->duplicates) n_dupes++;
      if (n_dupes > *max) *max = n_dupes;
    }
    files = files->next;
  }
  return groups;
}


static void registerpair(file_t **matchlist, file_t *newmatch, int (*comparef)(file_t *f1, file_t *f2))
{
  file_t *traverse;
  file_t *back;

  /* NULL pointer sanity checks */
  if (unlikely(matchlist == NULL || newmatch == NULL || comparef == NULL)) jc_nullptr("registerpair()");
  LOUD(fprintf(stderr, "registerpair: '%s', '%s'\n", (*matchlist)->d_name, newmatch->d_name);)

#ifndef NO_ERRORONDUPE
  if (ISFLAG(a_flags, FA_ERRORONDUPE)) {
    if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r");
    fprintf(stderr, "Exiting based on user request (-E); duplicates found:\n");
    printf("%s\n%s\n", (*matchlist)->d_name, newmatch->d_name);
    exit(255);
  }
#endif

  SETFLAG((*matchlist)->flags, FF_HAS_DUPES);
  back = NULL;
  traverse = *matchlist;

  /* FIXME: This needs to be changed! As it currently stands, the compare
   * function only runs on a pair as it is registered and future pairs can
   * mess up the sort order. A separate sorting function should happen before
   * the dupe chain is acted upon rather than while pairs are registered. */
  while (traverse) {
    if (comparef(newmatch, traverse) <= 0) {
      newmatch->duplicates = traverse;

      if (!back) {
        *matchlist = newmatch; /* update pointer to head of list */
        SETFLAG(newmatch->flags, FF_HAS_DUPES);
        CLEARFLAG(traverse->flags, FF_HAS_DUPES); /* flag is only for first file in dupe chain */
      } else back->duplicates = newmatch;

      break;
    } else {
      if (traverse->duplicates == 0) {
        traverse->duplicates = newmatch;
        if (!back) SETFLAG(traverse->flags, FF_HAS_DUPES);

        break;
      }
    }

    back = traverse;
    traverse = traverse->duplicates;
  }
  return;
}



#ifdef UNICODE
int wmain(int argc, wchar_t **wargv)
#else
int main(int argc, char **argv)
#endif
{
  static file_t *files = NULL;
  static file_t *curfile;
  static char **oldargv;
  static int firstrecurse;
  static int opt;
  static int pm = 1;
  static int partialonly_spec = 0;
#ifndef NO_MTIME  /* Remove if new order types are added! */
  static ordertype_t ordertype = ORDER_NAME;
#endif
#ifndef NO_CHUNKSIZE
  static long manual_chunk_size = 0;
 #ifdef __linux__
  static struct jc_proc_cacheinfo pci;
 #endif /* __linux__ */
#endif /* NO_CHUNKSIZE */
#ifdef ENABLE_DEDUPE
 #ifdef __linux__
  static struct utsname utsname;
 #endif /* __linux__ */
#endif

#ifndef NO_GETOPT_LONG
  static const struct option long_options[] =
  {
    { "loud", 0, 0, '@' },
    { "printnull", 0, 0, '0' }, //LEGACY
    { "print-null", 0, 0, '0' },
    { "one-file-system", 0, 0, '1' },
    { "", 0, 0, '9' },
    { "nohidden", 0, 0, 'A' }, //LEGACY
    { "no-hidden", 0, 0, 'A' },
    { "dedupe", 0, 0, 'B' },
    { "chunksize", 1, 0, 'C' }, //LEGACY
    { "chunk-size", 1, 0, 'C' },
    { "debug", 0, 0, 'D' },
    { "delete", 0, 0, 'd' },
    { "error-on-dupe", 0, 0, 'E' },
    { "omitfirst", 0, 0, 'f' }, //LEGACY
    { "omit-first", 0, 0, 'f' },
    { "hardlinks", 0, 0, 'H' }, //LEGACY
    { "hard-links", 0, 0, 'H' },
    { "help", 0, 0, 'h' },
    { "isolate", 0, 0, 'I' },
    { "reverse", 0, 0, 'i' },
    { "json", 0, 0, 'j' },
/*    { "skip-hash", 0, 0, 'K' }, */
    { "linkhard", 0, 0, 'L' }, //LEGACY
    { "link-hard", 0, 0, 'L' },
    { "linksoft", 0, 0, 'l' }, //LEGACY
    { "link-soft", 0, 0, 'l' },
    { "printwithsummary", 0, 0, 'M'}, //LEGACY
    { "print-summarize", 0, 0, 'M'},
    { "summarize", 0, 0, 'm'},
    { "noprompt", 0, 0, 'N' }, //LEGACY
    { "no-prompt", 0, 0, 'N' },
    { "paramorder", 0, 0, 'O' }, //LEGACY
    { "param-order", 0, 0, 'O' },
    { "order", 1, 0, 'o' },
    { "print", 1, 0, 'P' },
    { "permissions", 0, 0, 'p' },
    { "quick", 0, 0, 'Q' },
    { "quiet", 0, 0, 'q' },
    { "recurse:", 0, 0, 'R' },
    { "recurse", 0, 0, 'r' },
    { "size", 0, 0, 'S' },
    { "symlinks", 0, 0, 's' },
    { "partial-only", 0, 0, 'T' },
    { "nochangecheck", 0, 0, 't' }, //LEGACY
    { "no-change-check", 0, 0, 't' },
    { "notravcheck", 0, 0, 'U' }, //LEGACY
    { "no-trav-check", 0, 0, 'U' },
    { "printunique", 0, 0, 'u' }, //LEGACY
    { "print-unique", 0, 0, 'u' },
    { "version", 0, 0, 'v' },
    { "extfilter", 1, 0, 'X' }, //LEGACY
    { "ext-filter", 1, 0, 'X' },
    { "softabort", 0, 0, 'Z' }, //LEGACY
    { "soft-abort", 0, 0, 'Z' },
    { "zeromatch", 0, 0, 'z' }, //LEGACY
    { "zero-match", 0, 0, 'z' },
    { NULL, 0, 0, 0 }
  };
 #define GETOPT getopt_long
#else
 #define GETOPT getopt
#endif

#define GETOPT_STRING "@019ABC:DdEfHhIijKLlMmNnOo:P:pQqRrSsTtUuVvX:Zz"

  /* Verify libjodycode compatibility before going further */
  if (libjodycode_version_check(1, 0) != 0) {
    version_text(1);
    exit(EXIT_FAILURE);
  }

/* Windows buffers our stderr output; don't let it do that */
#ifdef ON_WINDOWS
  if (setvbuf(stderr, NULL, _IONBF, 0) != 0)
    fprintf(stderr, "warning: setvbuf() failed\n");
#endif

#ifdef UNICODE
  /* Create a UTF-8 **argv from the wide version */
  static char **argv;
  int wa_err;
  argv = (char **)malloc(sizeof(char *) * (size_t)argc);
  if (!argv) jc_oom("main() unicode argv");
  wa_err = jc_widearg_to_argv(argc, wargv, argv);
  if (wa_err != 0) {
    jc_print_error(wa_err);
    exit(EXIT_FAILURE);
  }
  /* fix up __argv so getopt etc. don't crash */
  __argv = argv;
  jc_set_output_modes(0x0c);
#endif /* UNICODE */

#ifndef NO_CHUNKSIZE
#ifdef __linux__
  /* Auto-tune chunk size to be half of L1 data cache if possible */
  jc_get_proc_cacheinfo(&pci);
  if (pci.l1 != 0) auto_chunk_size = (pci.l1 / 2);
  else if (pci.l1d != 0) auto_chunk_size = (pci.l1d / 2);
  /* Must be at least 4096 (4 KiB) and cannot exceed CHUNK_SIZE */
  if (auto_chunk_size < MIN_CHUNK_SIZE || auto_chunk_size > MAX_CHUNK_SIZE) auto_chunk_size = CHUNK_SIZE;
  /* Force to a multiple of 4096 if it isn't already */
  if ((auto_chunk_size & 0x00000fffUL) != 0)
    auto_chunk_size = (auto_chunk_size + 0x00000fffUL) & 0x000ff000;
#endif /* __linux__ */
#endif /* NO_CHUNKSIZE */

  /* Is stderr a terminal? If not, we won't write progress to it */
#ifdef ON_WINDOWS
  if (!_isatty(_fileno(stderr))) SETFLAG(flags, F_HIDEPROGRESS);
#else
  if (!isatty(fileno(stderr))) SETFLAG(flags, F_HIDEPROGRESS);
#endif

  program_name = argv[0];
  oldargv = cloneargs(argc, argv);

  while ((opt = GETOPT(argc, argv, GETOPT_STRING
#ifndef NO_GETOPT_LONG
          , long_options, NULL
#endif
         )) != EOF) {
    if ((uintptr_t)optarg == 0x20) goto error_optarg;
    switch (opt) {
    case '0':
      SETFLAG(a_flags, FA_PRINTNULL);
      LOUD(fprintf(stderr, "opt: print null instead of newline (--print-null)\n");)
      break;
    case '1':
      SETFLAG(flags, F_ONEFS);
      LOUD(fprintf(stderr, "opt: recursion across filesystems disabled (--one-file-system)\n");)
      break;
#ifdef DEBUG
    case '9':
      SETFLAG(flags, F_BENCHMARKSTOP);
      break;
#endif
    case 'A':
      SETFLAG(flags, F_EXCLUDEHIDDEN);
      break;
#ifndef NO_CHUNKSIZE
    case 'C':
      manual_chunk_size = strtol(optarg, NULL, 10) & 0x0ffff000L;  /* Align to 4K sizes */
      if (manual_chunk_size < MIN_CHUNK_SIZE || manual_chunk_size > MAX_CHUNK_SIZE) {
        fprintf(stderr, "warning: invalid manual chunk size (must be %d-%d); using defaults\n", MIN_CHUNK_SIZE, MAX_CHUNK_SIZE);
        LOUD(fprintf(stderr, "Manual chunk size (failed) was apparently '%s' => %ld\n", optarg, manual_chunk_size));
        manual_chunk_size = 0;
      } else auto_chunk_size = (size_t)manual_chunk_size;
      LOUD(fprintf(stderr, "Manual chunk size is %ld\n", manual_chunk_size));
      break;
#endif /* NO_CHUNKSIZE */
#ifndef NO_DELETE
    case 'd':
      SETFLAG(a_flags, FA_DELETEFILES);
      LOUD(fprintf(stderr, "opt: delete files after matching (--delete)\n");)
      break;
#endif /* NO_DELETE */
    case 'D':
#ifdef DEBUG
      SETFLAG(flags, F_DEBUG);
      break;
#endif
#ifndef NO_ERRORONDUPE
    case 'E':
      SETFLAG(a_flags, FA_ERRORONDUPE);
      break;
#endif /* NO_ERRORONDUPE */
    case 'f':
      SETFLAG(a_flags, FA_OMITFIRST);
      LOUD(fprintf(stderr, "opt: omit first match from each match set (--omit-first)\n");)
      break;
    case 'h':
      help_text();
      exit(EXIT_FAILURE);
#ifndef NO_HARDLINKS
    case 'H':
      SETFLAG(flags, F_CONSIDERHARDLINKS);
      LOUD(fprintf(stderr, "opt: hard links count as matches (--hard-links)\n");)
      break;
    case 'L':
      SETFLAG(a_flags, FA_HARDLINKFILES);
      LOUD(fprintf(stderr, "opt: convert duplicates to hard links (--link-hard)\n");)
      break;
#endif
    case 'i':
      SETFLAG(flags, F_REVERSESORT);
      LOUD(fprintf(stderr, "opt: sort order reversal enabled (--reverse)\n");)
      break;
#ifndef NO_USER_ORDER
    case 'I':
      SETFLAG(flags, F_ISOLATE);
      LOUD(fprintf(stderr, "opt: intra-parameter match isolation enabled (--isolate)\n");)
      break;
    case 'O':
      SETFLAG(flags, F_USEPARAMORDER);
      LOUD(fprintf(stderr, "opt: parameter order takes precedence (--param-order)\n");)
      break;
#else
    case 'I':
    case 'O':
      fprintf(stderr, "warning: -I and -O are disabled and ignored in this build\n");
      break;
#endif
#ifndef NO_JSON
    case 'j':
      SETFLAG(a_flags, FA_PRINTJSON);
      LOUD(fprintf(stderr, "opt: print output in JSON format (--print-json)\n");)
      break;
#endif /* NO_JSON */
    case 'K':
      SETFLAG(flags, F_SKIPHASH);
      break;
    case 'm':
      SETFLAG(a_flags, FA_SUMMARIZEMATCHES);
      LOUD(fprintf(stderr, "opt: print a summary of match stats (--summarize)\n");)
      break;
    case 'M':
      SETFLAG(a_flags, FA_SUMMARIZEMATCHES);
      SETFLAG(a_flags, FA_PRINTMATCHES);
      LOUD(fprintf(stderr, "opt: print matches with a summary (--print-summarize)\n");)
      break;
#ifndef NO_DELETE
    case 'N':
      SETFLAG(flags, F_NOPROMPT);
      LOUD(fprintf(stderr, "opt: delete files without prompting (--noprompt)\n");)
      break;
#endif /* NO_DELETE */
    case 'p':
      SETFLAG(flags, F_PERMISSIONS);
      LOUD(fprintf(stderr, "opt: permissions must also match (--permissions)\n");)
      break;
    case 'P':
      LOUD(fprintf(stderr, "opt: print early: '%s' (--print)\n", optarg);)
      if (jc_streq(optarg, "partial") == 0) SETFLAG(p_flags, PF_PARTIAL);
      else if (jc_streq(optarg, "early") == 0) SETFLAG(p_flags, PF_EARLYMATCH);
      else if (jc_streq(optarg, "fullhash") == 0) SETFLAG(p_flags, PF_FULLHASH);
      else {
        fprintf(stderr, "Option '%s' is not valid for -P\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 'q':
      SETFLAG(flags, F_HIDEPROGRESS);
      break;
    case 'Q':
      SETFLAG(flags, F_QUICKCOMPARE);
      fprintf(stderr, "\nBIG FAT WARNING: -Q/--quick MAY BE DANGEROUS! Read the manual!\n\n");
      LOUD(fprintf(stderr, "opt: byte-for-byte safety check disabled (--quick)\n");)
      break;
    case 'r':
      SETFLAG(flags, F_RECURSE);
      LOUD(fprintf(stderr, "opt: global recursion enabled (--recurse)\n");)
      break;
    case 'R':
      SETFLAG(flags, F_RECURSEAFTER);
      LOUD(fprintf(stderr, "opt: partial recursion enabled (--recurse-after)\n");)
      break;
    case 't':
      SETFLAG(flags, F_NOCHANGECHECK);
      LOUD(fprintf(stderr, "opt: TOCTTOU safety check disabled (--no-change-check)\n");)
      break;
    case 'T':
      partialonly_spec++;
      if (partialonly_spec == 1) {
      }
      if (partialonly_spec == 2) {
        SETFLAG(flags, F_PARTIALONLY);
        CLEARFLAG(flags, F_QUICKCOMPARE);
      }
      break;
    case 'u':
      SETFLAG(a_flags, FA_PRINTUNIQUE);
      LOUD(fprintf(stderr, "opt: print only non-matched (unique) files (--print-unique)\n");)
      break;
    case 'U':
      SETFLAG(flags, F_NOTRAVCHECK);
      LOUD(fprintf(stderr, "opt: double-traversal safety check disabled (--no-trav-check)\n");)
      break;
#ifndef NO_SYMLINKS
    case 'l':
      SETFLAG(a_flags, FA_MAKESYMLINKS);
      LOUD(fprintf(stderr, "opt: convert duplicates to symbolic links (--link-soft)\n");)
      break;
    case 's':
      SETFLAG(flags, F_FOLLOWLINKS);
      LOUD(fprintf(stderr, "opt: follow symbolic links enabled (--symlinks)\n");)
      break;
#endif
    case 'S':
      SETFLAG(a_flags, FA_SHOWSIZE);
      LOUD(fprintf(stderr, "opt: show size of files enabled (--size)\n");)
      break;
#ifndef NO_EXTFILTER
    case 'X':
      add_extfilter(optarg);
      break;
#endif /* NO_EXTFILTER */
    case 'z':
      SETFLAG(flags, F_INCLUDEEMPTY);
      LOUD(fprintf(stderr, "opt: zero-length files count as matches (--zero-match)\n");)
      break;
    case 'Z':
      SETFLAG(flags, F_SOFTABORT);
      LOUD(fprintf(stderr, "opt: soft-abort mode enabled (--soft-abort)\n");)
      break;
    case '@':
#ifdef LOUD_DEBUG
      SETFLAG(flags, F_DEBUG | F_LOUD | F_HIDEPROGRESS);
#endif
      LOUD(fprintf(stderr, "opt: loud debugging enabled, hope you can handle it (--loud)\n");)
      break;
    case 'v':
    case 'V':
      version_text(0);
      exit(EXIT_SUCCESS);
    case 'o':
#ifndef NO_MTIME  /* Remove if new order types are added! */
      if (!jc_strncaseeq("name", optarg, 5)) {
        ordertype = ORDER_NAME;
      } else if (!jc_strncaseeq("time", optarg, 5)) {
        ordertype = ORDER_TIME;
      } else {
        fprintf(stderr, "invalid value for --order: '%s'\n", optarg);
        exit(EXIT_FAILURE);
      }
#endif /* NO_MTIME */
      break;
    case 'B':
#ifdef ENABLE_DEDUPE
#ifdef __linux__
      /* Refuse to dedupe on 2.x kernels; they could damage user data */
      if (uname(&utsname)) {
        fprintf(stderr, "Failed to get kernel version! Aborting.\n");
        exit(EXIT_FAILURE);
      }
      LOUD(fprintf(stderr, "dedupefiles: uname got release '%s'\n", utsname.release));
      if (*(utsname.release) == '2' && *(utsname.release + 1) == '.') {
        fprintf(stderr, "Refusing to dedupe on a 2.x kernel; data loss could occur. Aborting.\n");
        exit(EXIT_FAILURE);
      }
#endif /* __linux__ */
      SETFLAG(a_flags, FA_DEDUPEFILES);
      /* btrfs will do the byte-for-byte check itself */
      if (!ISFLAG(flags, F_PARTIALONLY)) SETFLAG(flags, F_QUICKCOMPARE);
      /* It is completely useless to dedupe zero-length extents */
      CLEARFLAG(flags, F_INCLUDEEMPTY);
#else /* ENABLE_DEDUPE */
      fprintf(stderr, "This program was built without dedupe support\n");
      exit(EXIT_FAILURE);
#endif /* ENABLE_DEDUPE */
      LOUD(fprintf(stderr, "opt: CoW/block-level deduplication enabled (--dedupe)\n");)
      break;

    default:
      if (opt != '?') fprintf(stderr, "Sorry, using '-%c' is not supported in this build.\n", opt);
      fprintf(stderr, "Try `jdupes --help' for more information.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "no files or directories specified (use -h option for help)\n");
    exit(EXIT_FAILURE);
  }

  /* Make noise if people try to use -T because it's super dangerous */
  if (partialonly_spec > 0) {
    if (partialonly_spec > 2) {
      fprintf(stderr, "Saying -T three or more times? You're a wizard. No reminders for you.\n");
      goto skip_partialonly_noise;
    }
    fprintf(stderr, "\nBIG FAT WARNING: -T/--partial-only is EXTREMELY DANGEROUS! Read the manual!\n");
    fprintf(stderr,   "                 If used with destructive actions YOU WILL LOSE DATA!\n");
    fprintf(stderr,   "                 YOU ARE ON YOUR OWN. Use this power carefully.\n\n");
    if (partialonly_spec == 1) {
      fprintf(stderr, "-T is so dangerous that you must specify it twice to use it. By doing so,\n");
      fprintf(stderr, "you agree that you're OK with LOSING ALL OF YOUR DATA BY USING -T.\n\n");
      exit(EXIT_FAILURE);
    }
    if (partialonly_spec == 2) {
      fprintf(stderr, "You passed -T twice. I hope you know what you're doing. Last chance!\n\n");
      fprintf(stderr, "          HIT CTRL-C TO ABORT IF YOU AREN'T CERTAIN!\n          ");
      for (int countdown = 10; countdown > 0; countdown--) {
        fprintf(stderr, "%d, ", countdown);
        sleep(1);
      }
      fprintf(stderr, "bye-bye, data, it was nice knowing you.\n");
      fprintf(stderr, "For wizards: three tees is the way to be.\n\n");
    }
  }
skip_partialonly_noise:

  if (ISFLAG(flags, F_RECURSE) && ISFLAG(flags, F_RECURSEAFTER)) {
    fprintf(stderr, "options --recurse and --recurse: are not compatible\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(a_flags, FA_SUMMARIZEMATCHES) && ISFLAG(a_flags, FA_DELETEFILES)) {
    fprintf(stderr, "options --summarize and --delete are not compatible\n");
    exit(EXIT_FAILURE);
  }

#ifdef ENABLE_DEDUPE
  if (ISFLAG(flags, F_CONSIDERHARDLINKS) && ISFLAG(a_flags, FA_DEDUPEFILES))
    fprintf(stderr, "warning: option --dedupe overrides the behavior of --hardlinks\n");
#endif

  /* If pm == 0, call printmatches() */
  pm = !!ISFLAG(a_flags, FA_SUMMARIZEMATCHES) +
      !!ISFLAG(a_flags, FA_DELETEFILES) +
      !!ISFLAG(a_flags, FA_HARDLINKFILES) +
      !!ISFLAG(a_flags, FA_MAKESYMLINKS) +
      !!ISFLAG(a_flags, FA_PRINTJSON) +
      !!ISFLAG(a_flags, FA_PRINTUNIQUE) +
      !!ISFLAG(a_flags, FA_ERRORONDUPE) +
      !!ISFLAG(a_flags, FA_DEDUPEFILES);

  if (pm > 1) {
      fprintf(stderr, "Only one of --summarize, --print-summarize, --delete, --link-hard,\n--link-soft, --json, --error-on-dupe, or --dedupe may be used\n");
      exit(EXIT_FAILURE);
  }
  if (pm == 0) SETFLAG(a_flags, FA_PRINTMATCHES);

#ifndef ON_WINDOWS
  /* Catch SIGUSR1 and use it to enable -Z */
  signal(SIGUSR1, catch_sigusr1);
#endif

  /* Catch CTRL-C */
  signal(SIGINT, catch_interrupt);

  /* Progress indicator every second */
  if (!ISFLAG(flags, F_HIDEPROGRESS)) {
    jc_start_alarm(1, 1);
    /* Force an immediate progress update */
    jc_alarm_ring = 1;
  }

  if (ISFLAG(flags, F_RECURSEAFTER)) {
    firstrecurse = nonoptafter("--recurse:", argc, oldargv, argv);

    if (firstrecurse == argc)
      firstrecurse = nonoptafter("-R", argc, oldargv, argv);

    if (firstrecurse == argc) {
      fprintf(stderr, "-R option must be isolated from other options\n");
      exit(EXIT_FAILURE);
    }

    /* F_RECURSE is not set for directories before --recurse: */
    for (int x = optind; x < firstrecurse; x++) {
      if (interrupt) break;
      jc_slash_convert(argv[x]);
      loaddir(argv[x], &files, 0);
      user_item_count++;
    }

    /* Set F_RECURSE for directories after --recurse: */
    SETFLAG(flags, F_RECURSE);

    for (int x = firstrecurse; x < argc; x++) {
      if (interrupt) break;
      jc_slash_convert(argv[x]);
      loaddir(argv[x], &files, 1);
      user_item_count++;
    }
  } else {
    for (int x = optind; x < argc; x++) {
      if (interrupt) break;
      jc_slash_convert(argv[x]);
      loaddir(argv[x], &files, ISFLAG(flags, F_RECURSE));
      user_item_count++;
    }
  }

  /* Abort on CTRL-C (-Z doesn't matter yet) */
  if (interrupt) {
    fprintf(stderr, "%s", s_interrupt);
    exit(EXIT_FAILURE);
  }

  /* Force a progress update */
  if (!ISFLAG(flags, F_HIDEPROGRESS)) update_phase1_progress("items");

/* We don't need the double traversal check tree anymore */
#ifndef NO_TRAVCHECK
  travcheck_free(NULL);
#endif /* NO_TRAVCHECK */

#ifdef DEBUG
  /* Pass -9 option to exit after traversal/loading code */
  if (ISFLAG(flags, F_BENCHMARKSTOP)) {
    fprintf(stderr, "\nBenchmarking stop requested; exiting.\n");
    goto skip_all_scan_code;
  }
#endif

  if (ISFLAG(flags, F_REVERSESORT)) sort_direction = -1;
  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\n");
  if (!files) goto skip_file_scan;

  curfile = files;
  progress = 0;

  /* Force an immediate progress update */
  if (!ISFLAG(flags, F_HIDEPROGRESS)) jc_alarm_ring = 1;

  while (curfile) {
    static file_t **match = NULL;
    static FILE *file1;
    static FILE *file2;

    if (interrupt) {
      fprintf(stderr, "%s", s_interrupt);
      if (!ISFLAG(flags, F_SOFTABORT)) exit(EXIT_FAILURE);
      interrupt = 0;  /* reset interrupt for re-use */
      goto skip_file_scan;
    }

    LOUD(fprintf(stderr, "\nMAIN: current file: %s\n", curfile->d_name));

    if (!checktree) registerfile(&checktree, NONE, curfile);
    else match = checkmatch(checktree, curfile);

    /* Byte-for-byte check that a matched pair are actually matched */
    if (match != NULL) {
      /* Quick or partial-only compare will never run confirmmatch()
       * Also skip match confirmation for hard-linked files
       * (This set of comparisons is ugly, but quite efficient) */
      if (ISFLAG(flags, F_QUICKCOMPARE) || ISFLAG(flags, F_PARTIALONLY) ||
           (ISFLAG(flags, F_CONSIDERHARDLINKS) &&
           (curfile->inode == (*match)->inode) &&
           (curfile->device == (*match)->device))
         ) {
        LOUD(fprintf(stderr, "MAIN: notice: hard linked, quick, or partial-only match (-H/-Q/-T)\n"));
#ifndef NO_MTIME
        registerpair(match, curfile, (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
#else
        registerpair(match, curfile, sort_pairs_by_filename);
#endif
        dupecount++;
        goto skip_full_check;
      }

#ifdef UNICODE
      if (!M2W(curfile->d_name, wstr)) file1 = NULL;
      else file1 = _wfopen(wstr, FILE_MODE_RO);
#else
      file1 = fopen(curfile->d_name, FILE_MODE_RO);
#endif
      if (!file1) {
        LOUD(fprintf(stderr, "MAIN: warning: file1 fopen() failed ('%s')\n", curfile->d_name));
        curfile = curfile->next;
        continue;
      }

#ifdef UNICODE
      if (!M2W((*match)->d_name, wstr)) file2 = NULL;
      else file2 = _wfopen(wstr, FILE_MODE_RO);
#else
      file2 = fopen((*match)->d_name, FILE_MODE_RO);
#endif
      if (!file2) {
        fclose(file1);
        LOUD(fprintf(stderr, "MAIN: warning: file2 fopen() failed ('%s')\n", (*match)->d_name));
        curfile = curfile->next;
        continue;
      }

      if (confirmmatch(file1, file2, curfile->size)) {
        LOUD(fprintf(stderr, "MAIN: registering matched file pair\n"));
#ifndef NO_MTIME
        registerpair(match, curfile, (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
#else
        registerpair(match, curfile, sort_pairs_by_filename);
#endif
        dupecount++;
      } DBG(else hash_fail++;)

      fclose(file1);
      fclose(file2);
    }

skip_full_check:
    curfile = curfile->next;

    check_sigusr1();
    if (jc_alarm_ring != 0) {
      jc_alarm_ring = 0;
      update_phase2_progress(NULL, -1);
    }
    progress++;
  }

  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r%60s\r", " ");

skip_file_scan:
  /* Stop catching CTRL+C and firing alarms */
  signal(SIGINT, SIG_DFL);
  if (!ISFLAG(flags, F_HIDEPROGRESS)) jc_stop_alarm();
#ifndef NO_DELETE
  if (ISFLAG(a_flags, FA_DELETEFILES)) {
    if (ISFLAG(flags, F_NOPROMPT)) deletefiles(files, 0, 0);
    else deletefiles(files, 1, stdin);
  }
#endif /* NO_DELETE */
#ifndef NO_SYMLINKS
  if (ISFLAG(a_flags, FA_MAKESYMLINKS)) linkfiles(files, 0, 0);
#endif /* NO_SYMLINKS */
#ifndef NO_HARDLINKS
  if (ISFLAG(a_flags, FA_HARDLINKFILES)) linkfiles(files, 1, 0);
#endif /* NO_HARDLINKS */
#ifdef ENABLE_DEDUPE
  if (ISFLAG(a_flags, FA_DEDUPEFILES)) dedupefiles(files);
#endif /* ENABLE_DEDUPE */
  if (ISFLAG(a_flags, FA_PRINTMATCHES)) printmatches(files);
  if (ISFLAG(a_flags, FA_PRINTUNIQUE)) printunique(files);
#ifndef NO_JSON
  if (ISFLAG(a_flags, FA_PRINTJSON)) printjson(files, argc, argv);
#endif /* NO_JSON */
  if (ISFLAG(a_flags, FA_SUMMARIZEMATCHES)) {
    if (ISFLAG(a_flags, FA_PRINTMATCHES)) printf("\n\n");
    summarizematches(files);
  }

#ifdef DEBUG
skip_all_scan_code:
#endif

#ifdef DEBUG
  if (ISFLAG(flags, F_DEBUG)) {
    fprintf(stderr, "\n%d partial (+%d small) -> %d full hash -> %d full (%d partial elim) (%d hash%u fail)\n",
        partial_hash, small_file, full_hash, partial_to_full,
        partial_elim, hash_fail, (unsigned int)sizeof(jdupes_hash_t)*8);
    fprintf(stderr, "%" PRIuMAX " total files, %" PRIuMAX " comparisons\n", filecount, comparisons);
 #ifndef NO_CHUNKSIZE
    if (manual_chunk_size > 0) fprintf(stderr, "I/O chunk size: %ld KiB (manually set)\n", manual_chunk_size >> 10);
    else {
  #ifdef __linux__
      fprintf(stderr, "I/O chunk size: %" PRIuMAX " KiB (%s)\n", (uintmax_t)(auto_chunk_size >> 10), (pci.l1 + pci.l1d) != 0 ? "dynamically sized" : "default size");
  #else
      fprintf(stderr, "I/O chunk size: %" PRIuMAX " KiB (default size)\n", (uintmax_t)(auto_chunk_size >> 10));
  #endif /* __linux__ */
    }
 #endif /* NO_CHUNKSIZE */
 #ifdef ON_WINDOWS
  #ifndef NO_HARDLINKS
    if (ISFLAG(a_flags, FA_HARDLINKFILES))
      fprintf(stderr, "Exclusions based on Windows hard link limit: %u\n", hll_exclude);
  #endif
 #endif
  }
#endif /* DEBUG */

  exit(EXIT_SUCCESS);

error_optarg:
  fprintf(stderr, "error: option '%c' requires an argument\n", opt);
  exit(EXIT_FAILURE);
}
