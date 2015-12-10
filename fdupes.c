/* FDUPES Copyright (C) 1999-2015 Adrian Lopez
   Ported to MinGW by Jody Bruchon <jody@jodybruchon.com>
   Includes jody_hash (C) 2015 by Jody Bruchon

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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#ifndef OMIT_GETOPT_LONG
 #include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include "jody_hash.h"

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __CYGWIN__
 #define ON_WINDOWS 1
 #define NO_SYMLINKS 1
 #define NO_PERMS 1
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include "getino.h"
// #define NO_HARDLINKS 1
#endif

/* Compile out debugging stat counters unless requested */
#ifdef DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif


/* How many operations to wait before updating progress counters */
#define DELAY_COUNT 512

#define ISFLAG(a,b) ((a & b) == b)
#define SETFLAG(a,b) (a |= b)

/* Behavior modification flags */
static uint_fast32_t flags = 0;
#define F_RECURSE		0x00000001
#define F_HIDEPROGRESS		0x00000002
#define F_DSAMELINE		0x00000004
#define F_FOLLOWLINKS		0x00000008
#define F_DELETEFILES		0x00000010
#define F_EXCLUDEEMPTY		0x00000020
#define F_CONSIDERHARDLINKS	0x00000040
#define F_SHOWSIZE		0x00000080
#define F_OMITFIRST		0x00000100
#define F_RECURSEAFTER		0x00000200
#define F_NOPROMPT		0x00000400
#define F_SUMMARIZEMATCHES	0x00000800
#define F_EXCLUDEHIDDEN		0x00001000
#define F_PERMISSIONS		0x00002000
#define F_HARDLINKFILES		0x00004000
#define F_EXCLUDESIZE		0x00008000
#define F_QUICKCOMPARE		0x00010000
#define F_USEPARAMORDER		0x00020000
#define F_DEBUG			0x80000000

typedef enum {
  ORDER_TIME = 0,
  ORDER_NAME
} ordertype_t;

static const char *program_name;

static off_t excludesize = 0;

/* Larger chunk size makes large files process faster but uses more RAM */
#define CHUNK_SIZE 1048576
#define INPUT_SIZE 256
#define PARTIAL_HASH_SIZE 4096

/* These used to be functions. This way saves lots of call overhead */
#define getcrcsignature(a) getcrcsignatureuntil(a, 0)
#define getcrcpartialsignature(a) getcrcsignatureuntil(a, PARTIAL_HASH_SIZE)

/* TODO: Cachegrind indicates that size, inode, and device get hammered hard
 * in the checkmatch() code and trigger lots of cache line evictions.
 * Maybe we can compact these into a separate structure to improve speed.
 * Also look into compacting the true/false flags into one integer and see
 * if that improves performance (it'll certainly lower memory usage) */
typedef struct _file {
  char *d_name;
  uint_fast8_t valid_stat; /* Only call stat() once per file (1 = stat'ed) */
  off_t size;
  dev_t device;
  ino_t inode;
  mode_t mode;
#ifndef NO_PERMS
  uid_t uid;
  gid_t gid;
#endif
  time_t mtime;
  int user_order; /* Order of the originating command-line parameter */
  hash_t crcpartial;
  hash_t crcsignature;
  uint_fast8_t crcpartial_set;  /* 1 = crcpartial is valid */
  uint_fast8_t crcsignature_set;  /* 1 = crcsignature is valid */
  uint_fast8_t hasdupes; /* 1 only if file is first on duplicate chain */
  struct _file *duplicates;
  struct _file *next;
} file_t;

typedef struct _filetree {
  file_t *file;
  struct _filetree *left;
  struct _filetree *right;
#ifdef USE_TREE_REBALANCE
  struct _filetree *parent;
  unsigned int left_weight;
  unsigned int right_weight;
#endif /* USE_TREE_REBALANCE */
} filetree_t;

#ifdef USE_TREE_REBALANCE
static filetree_t *checktree = NULL;
#endif

static uintmax_t filecount = 0; // Required for progress indicator code

/* Hash/compare performance statistics (debug mode) */
#ifdef DEBUG
static unsigned int small_file = 0, partial_hash = 0, partial_to_full = 0, hash_fail = 0;
static uintmax_t comparisons = 0;
static unsigned int left_branch = 0, right_branch = 0;
static unsigned int tree_depth = 0, max_depth = 0;
#endif /* DEBUG */

/* Directory parameter position counter */
static unsigned int user_dir_count = 1;

/* registerfile() direction options */
enum tree_direction { NONE, LEFT, RIGHT };

/***** End definitions, begin code *****/

/*
 * String table allocator
 * A replacement for malloc() for tables of fixed strings
 *
 * Copyright (C) 2015 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2 (your choice)
 *
 * Included here using the license for this software
 * (Inlined for performance reasons)
 */

/* Must be divisible by uintptr_t */
#ifndef SMA_PAGE_SIZE
#define SMA_PAGE_SIZE 262144
#endif

static void *sma_head = NULL;
static uintptr_t *sma_lastpage = NULL;
static unsigned int sma_pages = 0;
static unsigned int sma_lastfree = 0;
static unsigned int sma_nextfree = sizeof(uintptr_t);


/*
static void dump_string_table(void)
{
	char *p = sma_head;
	unsigned int i = sizeof(uintptr_t);
	int pg = sma_pages;

	while (pg > 0) {
		while (i < SMA_PAGE_SIZE && *(p+i) == '\0') i++;
		printf("[%16p] (%jd) '%s'\n", p+i, strlen(p+i), p+i);
		i += strlen(p+i);
		if (pg <= 1 && i >= sma_nextfree) return;
		if (i < SMA_PAGE_SIZE) i++;
		else {
			p = (char *)*(uintptr_t *)p;
			pg--;
			i = sizeof(uintptr_t);
		}
		if (p == NULL) return;
	}

	return;
}
*/


static inline void *string_malloc_page(void)
{
	uintptr_t * restrict pageptr;

	/* Allocate page and set up pointers at page starts */
	pageptr = (uintptr_t *)malloc(SMA_PAGE_SIZE);
	if (pageptr == NULL) return NULL;
	*pageptr = (uintptr_t)NULL;

	/* Link previous page to this page, if applicable */
	if (sma_lastpage != NULL) *sma_lastpage = (uintptr_t)pageptr;

	/* Update last page pointers and total page counter */
	sma_lastpage = pageptr;
	sma_pages++;

	return (char *)pageptr;
}


static void *string_malloc(unsigned int len)
{
	const char * restrict page = (char *)sma_lastpage;
	static char *retval;

	/* Calling with no actual length is invalid */
	if (len < 1) return NULL;

	/* Align objects where possible */
	if (len & (sizeof(uintptr_t) - 1)) {
		len &= ~(sizeof(uintptr_t) - 1);
		len += sizeof(uintptr_t);
	}

	/* Refuse to allocate a space larger than we can store */
	if (len > (unsigned int)(SMA_PAGE_SIZE - sizeof(uintptr_t))) return NULL;

	/* Initialize on first use */
	if (sma_pages == 0) {
		sma_head = string_malloc_page();
		sma_nextfree = sizeof(uintptr_t);
		page = sma_head;
	}

	/* Allocate new pages when objects don't fit anymore */
	if ((sma_nextfree + len) > SMA_PAGE_SIZE) {
		page = string_malloc_page();
		sma_nextfree = sizeof(uintptr_t);
	}

	/* Allocate the space */
	retval = (char *)page + sma_nextfree;
	sma_lastfree = sma_nextfree;
	sma_nextfree += len;

	return retval;
}


/* Roll back the last allocation */
static inline void string_free(const void * const restrict addr)
{
	static const char * restrict p;

	/* Do nothing on NULL address or no last length */
	if (addr == NULL) return;
	if (sma_lastfree < sizeof(uintptr_t)) return;

	p = (char *)sma_lastpage + sma_lastfree;

	/* Only take action on the last pointer in the page */
	if ((uintptr_t)addr != (uintptr_t)p) return;

	sma_nextfree = sma_lastfree;
	sma_lastfree = 0;
	return;
}

/* Destroy all allocated pages */
static inline void string_malloc_destroy(void)
{
	static void *cur;
	static uintptr_t *next;

	cur = (void *)sma_head;
	while (sma_pages > 0) {
		next = (uintptr_t *)*(uintptr_t *)cur;
		free(cur);
		cur = (void *)next;
		sma_pages--;
	}
	sma_head = NULL;
	return;
}



/* Compare two jody_hashes like memcmp() */
static inline int crc_cmp(const hash_t hash1, const hash_t hash2)
{
	if (hash1 > hash2) return 1;
	if (hash1 == hash2) return 0;
	/* No need to compare a third time */
	return -1;
}


/* Print error message. NULL will output "out of memory" and exit */
static void errormsg(const char *message, ...)
{
  va_list ap;

  /* A null pointer means "out of memory" */
  if (message == NULL) {
    fprintf(stderr, "\r%40s\rout of memory\n", "");
    exit(EXIT_FAILURE);
  }

  va_start(ap, message);

  /* Windows will dump the full program path into argv[0] */
#ifndef ON_WINDOWS
  fprintf(stderr, "\r%40s\r%s: ", "", program_name);
#else
  fprintf(stderr, "\r%40s\r%s: ", "", "fdupes");
#endif
  vfprintf(stderr, message, ap);
}


static void escapefilename(char *escape_list, char **filename_ptr)
{
  static unsigned int x;
  static unsigned int tx;
  static char tmp[8192];
  static char *filename;
  static unsigned int sl;

  filename = *filename_ptr;
  sl = strlen(filename);

  for (x = 0, tx = 0; x < sl; x++) {
    if (tx >= 8192) errormsg("escapefilename() path overflow");
    if (strchr(escape_list, filename[x]) != NULL) tmp[tx++] = '\\';
    tmp[tx++] = filename[x];
  }

  tmp[tx] = '\0';

  if (x != tx) {
    //*filename_ptr = realloc(*filename_ptr, strlen(tmp) + 1);
    *filename_ptr = string_malloc(strlen(tmp) + 1);
    if (*filename_ptr == NULL) errormsg(NULL);
    strcpy(*filename_ptr, tmp);
  }
}


static inline char **cloneargs(const int argc, char **argv)
{
  static int x;
  static char **args;

  args = (char **) string_malloc(sizeof(char*) * argc);
  if (args == NULL) errormsg(NULL);

  for (x = 0; x < argc; x++) {
    args[x] = string_malloc(strlen(argv[x]) + 1);
    if (args[x] == NULL) errormsg(NULL);
    strcpy(args[x], argv[x]);
  }

  return args;
}


static int findarg(const char * const arg, const int start,
		const int argc, char **argv)
{
  static int x;

  for (x = start; x < argc; x++)
    if (strcmp(argv[x], arg) == 0)
      return x;

  return x;
}

/* Find the first non-option argument after specified option. */
static int nonoptafter(const char *option, const int argc,
		char **oldargv, char **newargv, int optind)
{
  static int x;
  static int targetind;
  static int testind;
  int startat = 1;

  targetind = findarg(option, 1, argc, oldargv);

  for (x = optind; x < argc; x++) {
    testind = findarg(newargv[x], startat, argc, oldargv);
    if (testind > targetind) return x;
    else startat = testind;
  }

  return x;
}


static inline void getfilestats(file_t * const restrict file)
{
  static struct stat s;

  /* Don't stat() the same file more than once */
  if (file->valid_stat == 1) return;
  file->valid_stat = 1;

  if (stat(file->d_name, &s) != 0) {
/* These are already set during file entry initialization */
    /* file->size = -1;
    file->inode = 0;
    file->device = 0;
    file->mtime = 0;
    file->mode = 0;
    file->uid = 0;
    file->gid = 0; */
    return;
  }
  file->size = s.st_size;
  file->device = s.st_dev;
  file->mtime = s.st_mtime;
  file->mode = s.st_mode;
#ifndef NO_PERMS
  file->uid = s.st_uid;
  file->gid = s.st_gid;
#endif
#ifdef ON_WINDOWS
  file->inode = getino(file->d_name);
#else
  file->inode = s.st_ino;
#endif /* ON_WINDOWS */
  return;
}


static void grokdir(const char * const restrict dir,
		file_t * restrict * const restrict filelistp)
{
  DIR *cd;
  file_t * restrict newfile;
  static int lastchar;
#ifndef NO_SYMLINKS
  static struct stat linfo;
#endif
  static struct dirent *dirinfo;
  static uintmax_t progress = 0, dir_progress = 0;
  static int grokdir_level = 0;
  static int delay = DELAY_COUNT;
  char *name;
  static char tempname[8192];

  cd = opendir(dir);
  dir_progress++;
  grokdir_level++;

  if (!cd) {
    errormsg("could not chdir to %s\n", dir);
    return;
  }

  while ((dirinfo = readdir(cd)) != NULL) {
    if (strcmp(dirinfo->d_name, ".") && strcmp(dirinfo->d_name, "..")) {
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        if (delay >= DELAY_COUNT) {
          delay = 0;
          fprintf(stderr, "\rScanning: %ju files, %ju dirs (in %u specified)",
			  progress, dir_progress, user_dir_count);
        } else delay++;
      }

      /* Assemble the file's full path name */
      strcpy(tempname, dir);
      lastchar = strlen(dir) - 1;
      if (lastchar >= 0 && dir[lastchar] != '/')
        strcat(tempname, "/");
      strcat(tempname, dirinfo->d_name);

      /* Allocate the file_t and the d_name entries in one shot
       * Reusing lastchar (with a +1) saves us a strlen(dir) here */
      newfile = (file_t *)string_malloc(sizeof(file_t) + lastchar + strlen(dirinfo->d_name) +3);
      if (!newfile) errormsg(NULL);
      else newfile->next = *filelistp;

      newfile->d_name = (char *)newfile + sizeof(file_t);
      newfile->user_order = user_dir_count;
      newfile->size = -1;
      newfile->device = 0;
      newfile->inode = 0;
      newfile->mtime = 0;
      newfile->mode = 0;
#ifndef NO_PERMS
      newfile->uid = 0;
      newfile->gid = 0;
#endif
      newfile->valid_stat = 0;
      newfile->crcsignature_set = 0;
      newfile->crcsignature = 0;
      newfile->crcpartial_set = 0;
      newfile->crcpartial = 0;
      newfile->duplicates = NULL;
      newfile->hasdupes = 0;

      strcpy(newfile->d_name, tempname);

      if (ISFLAG(flags, F_EXCLUDEHIDDEN)) {
        /* WARNING: Re-used tempname here to eliminate a strdup() */
        strcpy(tempname, newfile->d_name);
        name = basename(tempname);
        if (name[0] == '.' && strcmp(name, ".") && strcmp(name, "..")) {
          string_free((char *)newfile);
          continue;
        }
      }

      /* Get file information and check for validity */
      getfilestats(newfile);
      if (newfile->size == -1) {
	string_free((char *)newfile);
	continue;
      }

      /* Exclude zero-length files if requested */
      if (!S_ISDIR(newfile->mode) && newfile->size == 0 && ISFLAG(flags, F_EXCLUDEEMPTY)) {
	string_free((char *)newfile);
	continue;
      }

      /* Exclude files below --xsize parameter */
      if (!S_ISDIR(newfile->mode) && ISFLAG(flags, F_EXCLUDESIZE) && newfile->size < excludesize) {
	string_free((char *)newfile);
	continue;
      }

#ifndef NO_SYMLINKS
      /* Get lstat() information */
      if (lstat(newfile->d_name, &linfo) == -1) {
	string_free((char *)newfile);
	continue;
      }
#endif

      /* Optionally recurse directories, including symlinked ones if requested */
      if (S_ISDIR(newfile->mode)) {
#ifndef NO_SYMLINKS
	if (ISFLAG(flags, F_RECURSE) && (ISFLAG(flags, F_FOLLOWLINKS) || !S_ISLNK(linfo.st_mode)))
          grokdir(newfile->d_name, filelistp);
#else
	if (ISFLAG(flags, F_RECURSE))
          grokdir(newfile->d_name, filelistp);
#endif
	string_free((char *)newfile);
      } else {
        /* Add regular files to list, including symlink targets if requested */
#ifndef NO_SYMLINKS
        if (S_ISREG(linfo.st_mode) || (S_ISLNK(linfo.st_mode) && ISFLAG(flags, F_FOLLOWLINKS))) {
#else
        if (S_ISREG(newfile->mode)) {
#endif
	  *filelistp = newfile;
	  filecount++;
          progress++;
	} else {
	  string_free((char *)newfile);
	}
      }
    }
  }

  closedir(cd);

  grokdir_level--;
  if (grokdir_level == 0 && !ISFLAG(flags, F_HIDEPROGRESS)) {
    fprintf(stderr, "\rExamining %ju files, %ju dirs (in %u specified)",
            progress, dir_progress, user_dir_count);
  }
  return;
}

/* Use Jody Bruchon's hash function on part or all of a file */
static hash_t *getcrcsignatureuntil(const file_t * const restrict checkfile,
		const off_t max_read)
{
  off_t fsize;
  /* This is an array because we return a pointer to it */
  static hash_t hash[1];
  static hash_t chunk[(CHUNK_SIZE / sizeof(hash_t))];
  FILE *file;

  /* Get the file size. If we can't read it, bail out early */
  if (checkfile->size == -1) return NULL;
  fsize = checkfile->size;

  /* Do not read more than the requested number of bytes */
  if (max_read != 0 && fsize > max_read)
    fsize = max_read;

  /* Initialize the hash and file read parameters (with crcpartial skipped)
   *
   * If we already hashed the first chunk of this file, we don't want to
   * wastefully read and hash it again, so skip the first chunk and use
   * the computed hash for that chunk as our starting point.
   *
   * WARNING: We assume max_read is NEVER less than CHUNK_SIZE here! */

  if (checkfile->crcpartial_set) {
    *hash = checkfile->crcpartial;
    /* Don't bother going further if max_read is already fulfilled */
    if (max_read <= CHUNK_SIZE) return hash;
  } else *hash = 0;

  file = fopen(checkfile->d_name, "rb");
  if (file == NULL) {
    errormsg("error opening file %s\n", checkfile->d_name);
    return NULL;
  }

  /* Actually seek past the first chunk if applicable
   * This is part of the crcpartial skip optimization */
  if (checkfile->crcpartial_set) {
    if (!fseeko(file, CHUNK_SIZE, SEEK_SET)) {
      fclose(file);
      errormsg("error seeking in file %s\n", checkfile->d_name);
      return NULL;
    }
    fsize -= CHUNK_SIZE;
  }

  /* Read the file in CHUNK_SIZE chunks until we've read it all. */
  while (fsize > 0) {
    off_t bytes_to_read;

    bytes_to_read = (fsize >= CHUNK_SIZE) ? CHUNK_SIZE : fsize;
    if (fread((void *)chunk, bytes_to_read, 1, file) != 1) {
      errormsg("error reading from file %s\n", checkfile->d_name);
      fclose(file);
      return NULL;
    }

    *hash = jody_block_hash(chunk, *hash, bytes_to_read);
    if (bytes_to_read > fsize) break;
    else fsize -= bytes_to_read;
  }

  fclose(file);

  return hash;
}


static inline void purgetree(filetree_t * const restrict tree)
{
  if (tree->left != NULL) purgetree(tree->left);
  if (tree->right != NULL) purgetree(tree->right);
  string_free(tree);
}


static inline void registerfile(filetree_t * restrict * const restrict nodeptr,
		const enum tree_direction d, file_t * const restrict file)
{
	filetree_t * restrict branch;

	/* Allocate and initialize a new node for the file */
	branch = (filetree_t *)string_malloc(sizeof(filetree_t));
	if (branch == NULL) errormsg(NULL);
	branch->file = file;
	branch->left = NULL;
	branch->right = NULL;
#ifdef USE_TREE_REBALANCE
	branch->left_weight = 0;
	branch->right_weight = 0;

	/* Attach the new node to the requested branch and the parent */
	switch (d) {
		case LEFT:
			branch->parent = *nodeptr;
			(*nodeptr)->left = branch;
			(*nodeptr)->left_weight++;
			break;
		case RIGHT:
			branch->parent = *nodeptr;
			(*nodeptr)->right = branch;
			(*nodeptr)->right_weight++;
			break;
		case NONE:
			/* For the root of the tree only */
			branch->parent = NULL;
			*nodeptr = branch;
			break;
	}

	/* Propagate weights up the tree */
	while (branch->parent != NULL) {
		filetree_t * restrict up;

		up = branch->parent;
		if (up->left == branch) up->left_weight++;
		else if (up->right == branch) up->right_weight++;
		else {
			errormsg("Internal error: file tree linkage is broken\n");
			exit(EXIT_FAILURE);
		}
		branch = up;
	}
#else /* USE_TREE_REBALANCE */
	/* Attach the new node to the requested branch and the parent */
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
	}

#endif /* USE_TREE_REBALANCE */

	return;
}


/* Experimental tree rebalance code. This slows things down in testing
 * but may be more useful in the future. Pass -DUSE_TREE_REBALANCE
 * to try it. */
#ifdef USE_TREE_REBALANCE

/* How much difference to ignore when considering a rebalance */
#ifndef BALANCE_THRESHOLD
#define BALANCE_THRESHOLD 4
#endif
#ifndef BAL_BIT
#define BAL_BIT 0x2000
#endif

/* Rebalance the file tree to reduce search depth */
static inline void rebalance_tree(filetree_t * const tree)
{
	filetree_t * restrict promote;
	filetree_t * restrict demote;
	int difference, direction;
#ifdef CONSIDER_IMBALANCE
	int l, r, imbalance;
#endif

	if (!tree) return;

	/* Rebalance all children first */
	if (tree->left_weight > BALANCE_THRESHOLD) rebalance_tree(tree->left);
	if (tree->right_weight > BALANCE_THRESHOLD) rebalance_tree(tree->right);

	/* If weights are within a certain threshold, do nothing */
	direction = tree->right_weight - tree->left_weight;
	difference = direction;
	if (difference < 0) difference = -difference;
	if (difference <= BALANCE_THRESHOLD) return;

	/* Determine if a tree rotation will help, and do it if so */
	if (direction > 0) {
#ifdef CONSIDER_IMBALANCE
		l = tree->right->left_weight + tree->right_weight;
		r = tree->right->right_weight;
		imbalance = l - r;
		if (imbalance < 0) imbalance = -imbalance;
		/* Don't rotate if imbalance will increase */
		if (imbalance >= difference) return;
#endif /* CONSIDER_IMBALANCE */

		/* Rotate the right node up one level */
		promote = tree->right;
		demote = tree;
		/* Attach new parent's left tree to old parent */
		demote->right = promote->left;
		demote->right_weight = promote->left_weight;
		/* Attach old parent to new parent */
		promote->left = demote;
		promote->left_weight = demote->left_weight + demote->right_weight + 1;
		/* Reconnect parent linkages */
		promote->parent = demote->parent;
		if (demote->right) demote->right->parent = demote;
		demote->parent = promote;
		if (promote->parent == NULL) checktree = promote;
		else if (promote->parent->left == demote) promote->parent->left = promote;
		else promote->parent->right = promote;
		return;
	} else if (direction < 0) {
#ifdef CONSIDER_IMBALANCE
		r = tree->left->right_weight + tree->left_weight;
		l = tree->left->left_weight;
		imbalance = r - l;
		if (imbalance < 0) imbalance = -imbalance;
		/* Don't rotate if imbalance will increase */
		if (imbalance >= difference) return;
#endif /* CONSIDER_IMBALANCE */

		/* Rotate the left node up one level */
		promote = tree->left;
		demote = tree;
		/* Attach new parent's right tree to old parent */
		demote->left = promote->right;
		demote->left_weight = promote->right_weight;
		/* Attach old parent to new parent */
		promote->right = demote;
		promote->right_weight = demote->right_weight + demote->left_weight + 1;
		/* Reconnect parent linkages */
		promote->parent = demote->parent;
		if (demote->left) demote->left->parent = demote;
		demote->parent = promote;
		if (promote->parent == NULL) checktree = promote;
		else if (promote->parent->left == demote) promote->parent->left = promote;
		else promote->parent->right = promote;
		return;

	}

	/* Fall through */
	return;
}

#endif /* USE_TREE_REBALANCE */


static file_t **checkmatch(filetree_t * restrict tree,
		file_t * const restrict file)
{
  int cmpresult = 0;
  const hash_t * restrict crcsignature;

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
#ifndef NO_HARDLINKS
  if ((file->inode ==
      tree->file->inode) && (file->device ==
      tree->file->device)) {
    if (ISFLAG(flags, F_CONSIDERHARDLINKS)) return &tree->file;
    else return NULL;
  }
#endif

  /* Exclude files that are not the same size */
  if (file->size < tree->file->size) cmpresult = -1;
  else if (file->size > tree->file->size) cmpresult = 1;
  /* Exclude files by permissions if requested */
  else if (ISFLAG(flags, F_PERMISSIONS) &&
            (file->mode != tree->file->mode
#ifndef NO_PERMS
            || file->uid != tree->file->uid
            || file->gid != tree->file->gid
#endif
	    )) cmpresult = -1;
  else {
    /* Attempt to exclude files quickly with partial file hashing */
    DBG(partial_hash++;)
    if (tree->file->crcpartial_set == 0) {
      crcsignature = getcrcpartialsignature(tree->file);
      if (crcsignature == NULL) {
        errormsg("cannot read file %s\n", tree->file->d_name);
        return NULL;
      }

      tree->file->crcpartial = *crcsignature;
      tree->file->crcpartial_set = 1;
    }

    if (file->crcpartial_set == 0) {
      crcsignature = getcrcpartialsignature(file);
      if (crcsignature == NULL) {
        errormsg("cannot read file %s\n", file->d_name);
        return NULL;
      }

      file->crcpartial = *crcsignature;
      file->crcpartial_set = 1;
    }

    cmpresult = crc_cmp(file->crcpartial, tree->file->crcpartial);

    if (file->size <= PARTIAL_HASH_SIZE) {
      /* crcpartial = crcsignature if file is small enough */
      if (file->crcsignature_set == 0) {
        file->crcsignature = file->crcpartial;
        file->crcsignature_set = 1;
        DBG(small_file++;)
      }
      if (tree->file->crcsignature_set == 0) {
        tree->file->crcsignature = tree->file->crcpartial;
        tree->file->crcsignature_set = 1;
        DBG(small_file++;)
      }
    } else if (cmpresult == 0) {
      /* If partial match was correct, perform a full file hash match */
      if (tree->file->crcsignature_set == 0) {
	crcsignature = getcrcsignature(tree->file);
	if (crcsignature == NULL) return NULL;

	tree->file->crcsignature = *crcsignature;
        tree->file->crcsignature_set = 1;
      }

      if (file->crcsignature_set == 0) {
	crcsignature = getcrcsignature(file);
	if (crcsignature == NULL) return NULL;

	file->crcsignature = *crcsignature;
	file->crcsignature_set = 1;
      }

      /* Full file hash comparison */
      cmpresult = crc_cmp(file->crcsignature, tree->file->crcsignature);

    }
  }

  if (cmpresult < 0) {
    if (tree->left != NULL) {
      DBG(left_branch++; tree_depth++;)
      return checkmatch(tree->left, file);
    } else {
      registerfile(&tree, LEFT, file);
      DBG(if (max_depth < tree_depth) max_depth = tree_depth; tree_depth = 0;)
      return NULL;
    }
  } else if (cmpresult > 0) {
    if (tree->right != NULL) {
      DBG(right_branch++; tree_depth++;)
      return checkmatch(tree->right, file);
    } else {
      registerfile(&tree, RIGHT, file);
      DBG(if (max_depth < tree_depth) max_depth = tree_depth; tree_depth = 0;)
      return NULL;
    }
  } else {
    /* All compares matched */
    DBG(partial_to_full++;)
    DBG(if (max_depth < tree_depth) max_depth = tree_depth; tree_depth = 0;)
    return &tree->file;
  }
  /* Fall through - should never be reached */
  return NULL;
}


/* Do a byte-by-byte comparison in case two different files produce the
   same signature. Unlikely, but better safe than sorry. */
static inline int confirmmatch(FILE * const restrict file1, FILE * const restrict file2)
{
  static char c1[CHUNK_SIZE];
  static char c2[CHUNK_SIZE];
  static size_t r1;
  static size_t r2;

  fseek(file1, 0, SEEK_SET);
  fseek(file2, 0, SEEK_SET);

  do {
    r1 = fread(c1, sizeof(char), CHUNK_SIZE, file1);
    r2 = fread(c2, sizeof(char), CHUNK_SIZE, file2);

    if (r1 != r2) return 0; /* file lengths are different */
    if (memcmp (c1, c2, r1)) return 0; /* file contents are different */
  } while (r2);

  return 1;
}

static void summarizematches(const file_t * restrict files)
{
  unsigned int numsets = 0;
#ifdef NO_FLOAT
  off_t numbytes = 0;
#else
  double numbytes = 0.0;
#endif /* NO_FLOAT */
  int numfiles = 0;
  file_t *tmpfile;

  while (files != NULL) {
    if (files->hasdupes) {
      numsets++;
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
	numfiles++;
	numbytes += files->size;
	tmpfile = tmpfile->duplicates;
      }
    }
    files = files->next;
  }

  if (numsets == 0)
    printf("No duplicates found.\n");
  else
  {
    printf("%d duplicate files (in %d sets), occupying ", numfiles, numsets);
#ifdef NO_FLOAT
    if (numbytes < 1000) printf("%jd byte%c\n", numbytes, (numbytes != 1) ? 's' : ' ');
    else if (numbytes <= 1000000) printf("%jd KB\n", numbytes / 1000);
    else printf("%jd MB\n", numbytes / 1000000);
#else
    if (numbytes < 1000.0) printf("%.0f bytes\n", numbytes);
    else if (numbytes <= (1000.0 * 1000.0)) printf("%.1f KB\n", numbytes / 1000.0);
    else printf("%.1f MB\n", numbytes / (1000.0 * 1000.0));
#endif /* NO_FLOAT */
  }
}


static void printmatches(file_t * restrict files)
{
  file_t * restrict tmpfile;

  while (files != NULL) {
    if (files->hasdupes) {
      if (!ISFLAG(flags, F_OMITFIRST)) {
	if (ISFLAG(flags, F_SHOWSIZE)) printf("%jd byte%c each:\n", (intmax_t)files->size,
	 (files->size != 1) ? 's' : ' ');
	if (ISFLAG(flags, F_DSAMELINE)) escapefilename("\\ ", &files->d_name);
	printf("%s%c", files->d_name, ISFLAG(flags, F_DSAMELINE)?' ':'\n');
      }
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
	if (ISFLAG(flags, F_DSAMELINE)) escapefilename("\\ ", &tmpfile->d_name);
	printf("%s%c", tmpfile->d_name, ISFLAG(flags, F_DSAMELINE)?' ':'\n');
	tmpfile = tmpfile->duplicates;
      }
      printf("\n");

    }

    files = files->next;
  }
}


static void deletefiles(file_t *files, int prompt, FILE *tty)
{
  int counter;
  int groups = 0;
  int curgroup = 0;
  file_t *tmpfile;
  file_t *curfile;
  file_t **dupelist;
  int *preserve;
  char *preservestr;
  char *token;
  char *tstr;
  int number;
  int sum;
  int max = 0;
  int x, i;

  curfile = files;

  while (curfile) {
    if (curfile->hasdupes) {
      counter = 1;
      groups++;

      tmpfile = curfile->duplicates;
      while (tmpfile) {
	counter++;
	tmpfile = tmpfile->duplicates;
      }

      if (counter > max) max = counter;
    }

    curfile = curfile->next;
  }

  max++;

  dupelist = (file_t**) malloc(sizeof(file_t*) * max);
  preserve = (int*) malloc(sizeof(int) * max);
  preservestr = (char*) malloc(INPUT_SIZE);

  if (!dupelist || !preserve || !preservestr) errormsg(NULL);

  while (files) {
    if (files->hasdupes) {
      curgroup++;
      counter = 1;
      dupelist[counter] = files;

      if (prompt) printf("[%d] %s\n", counter, files->d_name);

      tmpfile = files->duplicates;

      while (tmpfile) {
	dupelist[++counter] = tmpfile;
	if (prompt) printf("[%d] %s\n", counter, tmpfile->d_name);
	tmpfile = tmpfile->duplicates;
      }

      if (prompt) printf("\n");

      /* preserve only the first file */
      if (!prompt) {
        preserve[1] = 1;
        for (x = 2; x <= counter; x++) preserve[x] = 0;
      } else do {
        /* prompt for files to preserve */
	printf("Set %d of %d: keep which files? (1 - %d, [a]ll)",
          curgroup, groups, counter);
	if (ISFLAG(flags, F_SHOWSIZE)) printf(" (%jd byte%c each)", (intmax_t)files->size,
	  (files->size != 1) ? 's' : ' ');
	printf(": ");
	fflush(stdout);

	if (!fgets(preservestr, INPUT_SIZE, tty))
	  preservestr[0] = '\n'; /* treat fgets() failure as if nothing was entered */

	i = strlen(preservestr) - 1;

        /* tail of buffer must be a newline */
	while (preservestr[i]!='\n') {
	  tstr = (char*)
	    realloc(preservestr, strlen(preservestr) + 1 + INPUT_SIZE);
	  if (!tstr) errormsg(NULL);

	  preservestr = tstr;
	  if (!fgets(preservestr + i + 1, INPUT_SIZE, tty))
	  {
	    preservestr[0] = '\n'; /* treat fgets() failure as if nothing was entered */
	    break;
	  }
	  i = strlen(preservestr)-1;
	}

	for (x = 1; x <= counter; x++) preserve[x] = 0;

	token = strtok(preservestr, " ,\n");

	while (token != NULL) {
	  if (*token == 'a' || *token == 'A')
	    for (x = 0; x <= counter; x++) preserve[x] = 1;

	  number = 0;
	  sscanf(token, "%d", &number);
	  if (number > 0 && number <= counter) preserve[number] = 1;

	  token = strtok(NULL, " ,\n");
	}

	for (sum = 0, x = 1; x <= counter; x++) sum += preserve[x];
      } while (sum < 1); /* save at least one file */

      printf("\n");

      for (x = 1; x <= counter; x++) {
	if (preserve[x])
	  printf("   [+] %s\n", dupelist[x]->d_name);
	else {
	  if (remove(dupelist[x]->d_name) == 0) {
	    printf("   [-] %s\n", dupelist[x]->d_name);
	  } else {
	    printf("   [!] %s ", dupelist[x]->d_name);
	    printf("-- unable to delete file!\n");
	  }
	}
      }
      printf("\n");
    }

    files = files->next;
  }

  free(dupelist);
  free(preserve);
  free(preservestr);
}


/* Unused
static inline int sort_pairs_by_arrival(file_t *f1, file_t *f2)
{
  if (f2->duplicates != 0) return 1;

  return -1;
}
*/


static int sort_pairs_by_param_order(file_t *f1, file_t *f2)
{
  if (!ISFLAG(flags, F_USEPARAMORDER)) return 0;
  if (f1->user_order < f2->user_order) return -1;
  if (f1->user_order > f2->user_order) return 1;
  return 0;
}


static int sort_pairs_by_mtime(file_t *f1, file_t *f2)
{
  int po = sort_pairs_by_param_order(f1, f2);

  if (po != 0) return po;

  if (f1->mtime < f2->mtime) return -1;
  else if (f1->mtime > f2->mtime) return 1;

  return 0;
}


#define IS_NUM(a) (((a >= '0') && (a <= '9')) ? 1 : 0)
static inline int numeric_sort(const char * restrict c1,
		const char * restrict c2)
{
	int len1 = 0, len2 = 0;
	int precompare = 0;

	/* Numerically correct sort */
	while (*c1 != '\0' && *c2 != '\0') {
		/* Reset string length counters */
		len1 = 0; len2 = 0;

		/* Skip all sequences of zeroes */
		while (*c1 == '0') {
			len1++;
			c1++;
		}
		while (*c2 == '0') {
			len2++;
			c2++;
		}

		/* If both chars are numeric, do a numeric comparison */
		if (IS_NUM(*c1) && IS_NUM(*c2)) {
			precompare = 0;

			/* Scan numbers and get preliminary results */
			while (IS_NUM(*c1) && IS_NUM(*c2)) {
				if (*c1 < *c2) precompare = -1;
				if (*c1 > *c2) precompare = 1;
				len1++; len2++;
				c1++; c2++;

				/* Skip remaining digit pairs after any
				 * difference is found */
				if (precompare != 0) {
					while (IS_NUM(*c1) && IS_NUM(*c2)) {
						len1++; len2++;
						c1++; c2++;
					}
					break;
				}
			}

			/* One numeric and one non-numeric means the
			 * numeric one is larger and sorts later */
			if (IS_NUM(*c1) ^ IS_NUM(*c2)) {
				if (IS_NUM(*c1)) return 1;
				else return -1;
			}

			/* If the last test fell through, numbers are
			 * of equal length. Use the precompare result
			 * as the result for this number comparison. */
			if (precompare != 0) return precompare;
		}

		/* Do normal comparison */
		if (*c1 == *c2) {
			c1++; c2++;
			len1++; len2++;
		/* Put symbols and spaces after everything else */
		} else if (*c2 < '.' && *c1 >= '.') return -1;
		else if (*c1 < '.' && *c2 >= '.') return 1;
		/* Normal strcmp() style compare */
		else if (*c1 > *c2) return 1;
		else return -1;
	}

	/* Longer strings generally sort later */
	if (len1 < len2) return -1;
	if (len1 > len2) return 1;
	/* Normal strcmp() style comparison */
	if (*c1 == '\0' && *c2 != '\0') return -1;
	if (*c1 != '\0' && *c2 == '\0') return 1;

	/* Fall through: the strings are equal */
	return 0;
}


static int sort_pairs_by_filename(file_t *f1, file_t *f2)
{
  int po = sort_pairs_by_param_order(f1, f2);

  if (po != 0) return po;

  return numeric_sort(f1->d_name, f2->d_name);
}


static void registerpair(file_t **matchlist, file_t *newmatch,
		  int (*comparef)(file_t *f1, file_t *f2))
{
  file_t *traverse;
  file_t *back;

  (*matchlist)->hasdupes = 1;
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
	newmatch->hasdupes = 1;
	traverse->hasdupes = 0; /* flag is only for first file in dupe chain */
      } else back->duplicates = newmatch;

      break;
    } else {
      if (traverse->duplicates == 0) {
	traverse->duplicates = newmatch;
	if(!back) traverse->hasdupes = 1;

	break;
      }
    }

    back = traverse;
    traverse = traverse->duplicates;
  }
}


#ifndef NO_HARDLINKS
static inline void hardlinkfiles(file_t *files)
{
  static file_t *tmpfile;
  static file_t *curfile;
  static file_t ** restrict dupelist;
  static int counter;
  static int max = 0;
  static int x = 0;
  static int i;
  static char temp_path[4096];

  curfile = files;

  while (curfile) {
    if (curfile->hasdupes) {
      counter = 1;
      tmpfile = curfile->duplicates;
      while (tmpfile) {
       counter++;
       tmpfile = tmpfile->duplicates;
      }

      if (counter > max) max = counter;
    }

    curfile = curfile->next;
  }

  max++;

  dupelist = (file_t**) malloc(sizeof(file_t*) * max);

  if (!dupelist) errormsg(NULL);

  while (files) {
    if (files->hasdupes) {
      counter = 1;
      dupelist[counter] = files;

      tmpfile = files->duplicates;

      while (tmpfile) {
       counter++;
       dupelist[counter] = tmpfile;
       tmpfile = tmpfile->duplicates;
      }

      /* Link every file to the first file */

      if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("[SRC] %s\n", dupelist[1]->d_name);
      for (x = 2; x <= counter; x++) {
        /* Can't hard link files on different devices */
        if (dupelist[1]->device != dupelist[x]->device) {
	  fprintf(stderr, "warning: hard link target on different device, not linking:\n-//-> %s\n",
		  dupelist[x]->d_name);
	  continue;
	} else {
          /* The devices for the files are the same, but we still need to skip
           * anything that is already hard linked (-L and -H both set) */
          if (dupelist[1]->inode == dupelist[x]->inode) {
            if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("-==-> %s\n", dupelist[x]->d_name);
            continue;
          }
        }
        /* Do not attempt to hard link files for which we don't have write access */
	if (access(dupelist[x]->d_name, W_OK) != 0) {
	  fprintf(stderr, "warning: hard link target is a read-only file, not linking:\n-//-> %s\n",
		  dupelist[x]->d_name);
	  continue;
	}
        /* Safe hard linking: don't actually delete until the link succeeds */
        strcpy(temp_path, dupelist[x]->d_name);
        strcat(temp_path, "._fd_tmp");
        i = rename(dupelist[x]->d_name, temp_path);
        if (i != 0) {
	  fprintf(stderr, "warning: cannot move hard link target to a temporary name, not linking:\n-//-> %s\n",
		  dupelist[x]->d_name);
          continue;
        }

	errno = 0;
#ifdef ON_WINDOWS
        if (CreateHardLink(dupelist[x]->d_name, dupelist[1]->d_name, NULL) == TRUE) {
#else
        if (link(dupelist[1]->d_name, dupelist[x]->d_name) == 0) {
#endif /* ON_WINDOWS */
          if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("----> %s\n", dupelist[x]->d_name);
        } else {
          /* The hard link failed. Warn the user and put the link target back */
          if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("-//-> %s ", dupelist[x]->d_name);
	  fprintf(stderr, "warning: unable to hard link '%s' -> '%s': %s\n",
			  dupelist[x]->d_name, dupelist[1]->d_name, strerror(errno));
          i = rename(temp_path, dupelist[x]->d_name);
	  if (i != 0) {
		  fprintf(stderr, "error: cannot rename temp file back to original\n");
		  fprintf(stderr, "original: %s\n", dupelist[x]->d_name);
		  fprintf(stderr, "current:  %s\n", temp_path);
	  }
	  continue;
        }
        i = unlink(temp_path);
	if (i != 0) fprintf(stderr, "\nwarning: can't delete temp file: %s\n", temp_path);
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("\n");
    }
    files = files->next;
  }

  free(dupelist);
}
#endif /* NO_HARDLINKS */


static inline void help_text(void)
{
  printf("Usage: fdupes [options] DIRECTORY...\n\n");

  printf(" -r --recurse     \tfor every directory given follow subdirectories\n");
  printf("                  \tencountered within\n");
  printf(" -R --recurse:    \tfor each directory given after this option follow\n");
  printf("                  \tsubdirectories encountered within (note the ':' at\n");
  printf("                  \tthe end of the option, manpage for more details)\n");
#ifndef NO_SYMLINKS
  printf(" -s --symlinks    \tfollow symlinks\n");
#endif
#ifndef NO_HARDLINKS
  printf(" -H --hardlinks   \tnormally, when two or more files point to the same\n");
  printf("                  \tdisk area they are treated as non-duplicates; this\n");
  printf("                  \toption will change this behavior\n");
  printf(" -L --linkhard    \thard link duplicate files to the first file in\n");
  printf("                  \teach set of duplicates without prompting the user\n");
#endif
  printf(" -n --noempty     \texclude zero-length files from consideration\n");
  printf(" -x --xsize=SIZE  \texclude files of size < SIZE from consideration; the\n");
  printf("                  \tSIZE argument accepts 'K', 'M' and 'G' unit suffix\n");
  printf(" -A --nohidden    \texclude hidden files from consideration\n");
  printf(" -f --omitfirst   \tomit the first file in each set of matches\n");
  printf(" -1 --sameline    \tlist each set of matches on a single line\n");
  printf(" -S --size        \tshow size of duplicate files\n");
  printf(" -m --summarize   \tsummarize dupe information\n");
  printf(" -q --quiet       \thide progress indicator\n");
/* This is undocumented in the quick help because it is a dangerous option. If you
 * really want it, uncomment it here, and may your data rest in peace. */
/*  printf(" -Q --quick       \tskip byte-by-byte duplicate verification. WARNING:\n");
  printf("                  \tthis may delete non-duplicates! Read the manual first!\n"); */
  printf(" -d --delete      \tprompt user for files to preserve and delete all\n");
  printf("                  \tothers; important: under particular circumstances,\n");
  printf("                  \tdata may be lost when using this option together\n");
  printf("                  \twith -s or --symlinks, or when specifying a\n");
  printf("                  \tparticular directory more than once; refer to the\n");
  printf("                  \tfdupes documentation for additional information\n");
  printf(" -N --noprompt    \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
#ifndef NO_PERMS
  printf(" -p --permissions \tdon't consider files with different owner/group or\n");
  printf("                  \tpermission bits as duplicates\n");
#endif
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
  printf("                  \tmtime (BY=time; default) or filename (BY=name)\n");
  printf(" -O --paramorder  \tParameter order is more important than selected -O sort\n");
  printf(" -v --version     \tdisplay fdupes version and license information\n");
  printf(" -h --help        \tdisplay this help message\n\n");
#ifdef OMIT_GETOPT_LONG
  printf("Note: Long options are not supported in this fdupes build.\n\n");
#endif
}

int main(int argc, char **argv) {
  static file_t *files = NULL;
  static file_t *curfile;
  static char **oldargv;
  static char *endptr;
#ifndef USE_TREE_REBALANCE
    filetree_t *checktree = NULL;
#endif
  static int firstrecurse;
  static int opt;
  static ordertype_t ordertype = ORDER_TIME;

#ifndef OMIT_GETOPT_LONG
  static struct option long_options[] =
  {
    { "omitfirst", 0, 0, 'f' },
    { "recurse", 0, 0, 'r' },
    { "recursive", 0, 0, 'r' },
    { "recurse:", 0, 0, 'R' },
    { "recursive:", 0, 0, 'R' },
    { "quiet", 0, 0, 'q' },
    { "quick", 0, 0, 'Q' },
    { "sameline", 0, 0, '1' },
    { "size", 0, 0, 'S' },
#ifndef NO_SYMLINKS
    { "symlinks", 0, 0, 's' },
#endif
#ifndef NO_HARDLINKS
    { "hardlinks", 0, 0, 'H' },
    { "linkhard", 0, 0, 'L' },
#endif
    { "noempty", 0, 0, 'n' },
    { "xsize", 1, 0, 'x' },
    { "nohidden", 0, 0, 'A' },
    { "delete", 0, 0, 'd' },
    { "debug", 0, 0, 'D' },
    { "version", 0, 0, 'v' },
    { "help", 0, 0, 'h' },
    { "noprompt", 0, 0, 'N' },
    { "summarize", 0, 0, 'm'},
    { "summary", 0, 0, 'm' },
#ifndef NO_PERMS
    { "permissions", 0, 0, 'p' },
#endif
    { "paramorder", 0, 0, 'O' },
    { "order", 1, 0, 'o' },
    { 0, 0, 0, 0 }
  };
#define GETOPT getopt_long
#else
#define GETOPT getopt
#endif

  program_name = argv[0];

  oldargv = cloneargs(argc, argv);

  while ((opt = GETOPT(argc, argv,
  "frRqQ1SsHLnx:AdDvhNmpo:O"
#ifndef OMIT_GETOPT_LONG
          , long_options, NULL
#endif
	  )) != EOF) {
    switch (opt) {
    case 'f':
      SETFLAG(flags, F_OMITFIRST);
      break;
    case 'r':
      SETFLAG(flags, F_RECURSE);
      break;
    case 'R':
      SETFLAG(flags, F_RECURSEAFTER);
      break;
    case 'q':
      SETFLAG(flags, F_HIDEPROGRESS);
      break;
    case 'Q':
      SETFLAG(flags, F_QUICKCOMPARE);
      break;
    case '1':
      SETFLAG(flags, F_DSAMELINE);
      break;
    case 'S':
      SETFLAG(flags, F_SHOWSIZE);
      break;
#ifndef NO_SYMLINKS
    case 's':
      SETFLAG(flags, F_FOLLOWLINKS);
      break;
#endif
#ifndef NO_HARDLINKS
    case 'H':
      SETFLAG(flags, F_CONSIDERHARDLINKS);
      break;
    case 'L':
      SETFLAG(flags, F_HARDLINKFILES);
      break;
#endif
    case 'n':
      SETFLAG(flags, F_EXCLUDEEMPTY);
      break;
    case 'x':
      SETFLAG(flags, F_EXCLUDESIZE);
      excludesize = strtoull(optarg, &endptr, 0);
      switch (*endptr) {
        case 'k':
	case 'K':
          excludesize = excludesize * 1024;
          endptr++;
          break;
        case 'm':
        case 'M':
          excludesize = excludesize * 1024 * 1024;
          endptr++;
          break;
        case 'g':
        case 'G':
          excludesize = excludesize * 1024 * 1024 * 1024;
          endptr++;
          break;
        default:
          break;
      }
      if (*endptr != '\0') {
        errormsg("invalid value for --xsize: '%s'\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 'A':
      SETFLAG(flags, F_EXCLUDEHIDDEN);
      break;
    case 'd':
      SETFLAG(flags, F_DELETEFILES);
      break;
    case 'D':
#ifdef DEBUG
      SETFLAG(flags, F_DEBUG);
#endif
      break;
    case 'v':
      printf("fdupes-jody %s\n", VERSION);
      printf("Copyright (C) 1999-2015 Adrian Lopez\n");
#ifdef ON_WINDOWS
      printf("Ported to Windows (MinGW-w64) by Jody Bruchon\n");
#endif
      printf("Includes jody_hash (C) 2015 by Jody Bruchon <jody@jodybruchon.com>\n\n");
      printf("Permission is hereby granted, free of charge, to any person\n");
      printf("obtaining a copy of this software and associated documentation files\n");
      printf("(the \"Software\"), to deal in the Software without restriction,\n");
      printf("including without limitation the rights to use, copy, modify, merge,\n");
      printf("publish, distribute, sublicense, and/or sell copies of the Software,\n");
      printf("and to permit persons to whom the Software is furnished to do so,\n");
      printf("subject to the following conditions:\n\n");
      printf("The above copyright notice and this permission notice shall be\n");
      printf("included in all copies or substantial portions of the Software.\n\n");
      printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS\n");
      printf("OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF\n");
      printf("MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\n");
      printf("IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY\n");
      printf("CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,\n");
      printf("TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE\n");
      printf("SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n");
      exit(EXIT_SUCCESS);
    case 'h':
      help_text();
      exit(EXIT_FAILURE);
    case 'N':
      SETFLAG(flags, F_NOPROMPT);
      break;
    case 'm':
      SETFLAG(flags, F_SUMMARIZEMATCHES);
      break;
    case 'p':
      SETFLAG(flags, F_PERMISSIONS);
      break;
    case 'O':
      SETFLAG(flags, F_USEPARAMORDER);
      break;
    case 'o':
      if (!strncasecmp("name", optarg, 5)) {
        ordertype = ORDER_NAME;
      } else if (!strncasecmp("time", optarg, 5)) {
        ordertype = ORDER_TIME;
      } else {
        errormsg("invalid value for --order: '%s'\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;

    default:
      fprintf(stderr, "Try `fdupes --help' for more information.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    errormsg("no directories specified\n");
    exit(EXIT_FAILURE);
  }
#ifndef NO_HARDLINKS
  if (ISFLAG(flags, F_HARDLINKFILES) && ISFLAG(flags, F_DELETEFILES)) {
    errormsg("options --linkhard and --delete are not compatible\n");
    exit(EXIT_FAILURE);
  }

#endif	/* NO_HARDLINKS */
  if (ISFLAG(flags, F_RECURSE) && ISFLAG(flags, F_RECURSEAFTER)) {
    errormsg("options --recurse and --recurse: are not compatible\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_SUMMARIZEMATCHES) && ISFLAG(flags, F_DELETEFILES)) {
    errormsg("options --summarize and --delete are not compatible\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_RECURSEAFTER)) {
    firstrecurse = nonoptafter("--recurse:", argc, oldargv, argv, optind);

    if (firstrecurse == argc)
      firstrecurse = nonoptafter("-R", argc, oldargv, argv, optind);

    if (firstrecurse == argc) {
      errormsg("-R option must be isolated from other options\n");
      exit(EXIT_FAILURE);
    }

    /* F_RECURSE is not set for directories before --recurse: */
    for (int x = optind; x < firstrecurse; x++) {
      grokdir(argv[x], &files);
      user_dir_count++;
    }

    /* Set F_RECURSE for directories after --recurse: */
    SETFLAG(flags, F_RECURSE);

    for (int x = firstrecurse; x < argc; x++) {
      grokdir(argv[x], &files);
      user_dir_count++;
    }
  } else {
    for (int x = optind; x < argc; x++) {
      grokdir(argv[x], &files);
      user_dir_count++;
    }
  }

//  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r%60s\r", " ");
  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\n");
  if (!files) exit(EXIT_SUCCESS);

  curfile = files;

  while (curfile) {
    static uintmax_t progress = 0;
    static uintmax_t dupecount = 0;
    static file_t **match = NULL;
    static FILE *file1;
    static FILE *file2;
    static unsigned int delay = DELAY_COUNT;

    if (!checktree) registerfile(&checktree, NONE, curfile);
    else match = checkmatch(checktree, curfile);

#ifdef USE_TREE_REBALANCE
    /* Rebalance the match tree after a certain number of files processed */
    if ((progress & ((BAL_BIT << 1) - 1)) == BAL_BIT) rebalance_tree(checktree);
#endif /* USE_TREE_REBALANCE */

    /* Byte-for-byte check that a matched pair are actually matched */
    if (match != NULL) {
      /* Quick comparison mode will never run confirmmatch()
       * Also skip match confirmation for hard-linked files
       * (This set of comparisons is ugly, but quite efficient) */
      if (ISFLAG(flags, F_QUICKCOMPARE) ||
		(ISFLAG(flags, F_CONSIDERHARDLINKS) &&
		 (curfile->inode == (*match)->inode) &&
		 (curfile->device == (*match)->device))
		) {
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
	dupecount++;
	goto skip_full_check;
      }

      file1 = fopen(curfile->d_name, "rb");
      if (!file1) {
	curfile = curfile->next;
	continue;
      }

      file2 = fopen((*match)->d_name, "rb");
      if (!file2) {
	fclose(file1);
	curfile = curfile->next;
	continue;
      }

      if (confirmmatch(file1, file2)) {
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
	dupecount++;
      } DBG(else hash_fail++;)

      fclose(file1);
      fclose(file2);
    }

skip_full_check:
    curfile = curfile->next;

    if (!ISFLAG(flags, F_HIDEPROGRESS)) {
      /* If file size is larger than 1 MiB, make progress update faster
       * If confirmmatch() is run on a file, speed up progress even further */
      if (curfile != NULL) delay += (curfile->size >> 20);
      if (match != NULL) delay++;
      if ((delay >= DELAY_COUNT)) {
        delay = 0;
        fprintf(stderr, "\rProgress [%ju/%ju, %ju pairs matched] %ju%%", progress, filecount,
          dupecount, (progress * 100) / filecount);
      } else delay++;
    }
    progress++;
  }

  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r%60s\r", " ");

  if (ISFLAG(flags, F_DELETEFILES)) {
    if (ISFLAG(flags, F_NOPROMPT)) deletefiles(files, 0, 0);
    else deletefiles(files, 1, stdin);
  } else {
#ifndef NO_HARDLINKS
    if (ISFLAG(flags, F_HARDLINKFILES)) {
      if (ISFLAG(flags, F_SUMMARIZEMATCHES)) summarizematches(files);
      hardlinkfiles(files);
    }
#else
    if (0) {}
#endif /* NO_HARDLINKS */
    else {
      if (ISFLAG(flags, F_SUMMARIZEMATCHES)) summarizematches(files);
      else printmatches(files);
    }
  }

  /*
  while (files) {
    curfile = files->next;
    free(files);
    files = curfile;
  }
  */

  purgetree(checktree);
  string_malloc_destroy();

#ifdef DEBUG
  if (ISFLAG(flags, F_DEBUG)) {
    fprintf(stderr, "\n%d partial (+%d small) -> %d full (%d partial elim) (%d hash fail)\n",
		partial_hash, small_file, partial_to_full,
		(partial_hash - partial_to_full), hash_fail);
    fprintf(stderr, "%ju total files, %ju comparisons, branch L %u, R %u, both %u\n",
		    filecount, comparisons, left_branch, right_branch,
		    left_branch + right_branch);
    fprintf(stderr, "Maximum tree depth reached: %u\n", max_depth);
  }
#endif /* DEBUG */

  exit(EXIT_SUCCESS);

}
