/* FDUPES Copyright (c) 1999-2002 Adrian Lopez
   Ported to MinGW by Jody Bruchon <jody@jodybruchon.com>

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
 #define NO_HARDLINKS 1
#endif

/* How many operations to wait before updating progress counters */
#define DELAY_COUNT 512

#define ISFLAG(a,b) ((a & b) == b)
#define SETFLAG(a,b) (a |= b)

/* Behavior modification flags */
uint_fast32_t flags = 0;
#define F_RECURSE           0x000001
#define F_HIDEPROGRESS      0x000002
#define F_DSAMELINE         0x000004
#define F_FOLLOWLINKS       0x000008
#define F_DELETEFILES       0x000010
#define F_EXCLUDEEMPTY      0x000020
#define F_CONSIDERHARDLINKS 0x000040
#define F_SHOWSIZE          0x000080
#define F_OMITFIRST         0x000100
#define F_RECURSEAFTER      0x000200
#define F_NOPROMPT          0x000400
#define F_SUMMARIZEMATCHES  0x000800
#define F_EXCLUDEHIDDEN     0x001000
#define F_PERMISSIONS       0x002000
#define F_HARDLINKFILES     0x004000
#define F_EXCLUDESIZE       0x008000

typedef enum {
  ORDER_TIME = 0,
  ORDER_NAME
} ordertype_t;

const char *program_name;

off_t excludesize = 0;

#define CHUNK_SIZE 8192
#define INPUT_SIZE 256
#define PARTIAL_HASH_SIZE 4096

/* These used to be functions. This way saves lots of call overhead */
#define getcrcsignature(a) getcrcsignatureuntil(a, 0)
#define getcrcpartialsignature(a) getcrcsignatureuntil(a, PARTIAL_HASH_SIZE)

typedef struct _file {
  char *d_name;
  off_t size;
  hash_t crcpartial;
  hash_t crcsignature;
  dev_t device;
  ino_t inode;
  time_t mtime;
  uint_fast8_t hasdupes; /* true only if file is first on duplicate chain */
  uint_fast8_t crcpartial_set;  /* 1 = crcpartial is valid */
  uint_fast8_t crcsignature_set;  /* 1 = crcsignature is valid */
  struct _file *duplicates;
  struct _file *next;
} file_t;

typedef struct _filetree {
  file_t *file;
  struct _filetree *left;
  struct _filetree *right;
} filetree_t;

/* Hash/compare performance statistics */
int small_file = 0, partial_hash = 0, partial_to_full = -1, hash_fail = 0;


/***** End definitions, begin code *****/


/* Compare two jody_hashes like memcmp() */
static inline int crc_cmp(const hash_t hash1, const hash_t hash2)
{
	if (hash1 > hash2) return 1;
	if (hash1 == hash2) return 0;
	/* No need to compare a third time */
	return -1;
}


static void errormsg(char *message, ...)
{
  va_list ap;

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
  int x;
  int tx;
  char *tmp;
  char *filename;

  filename = *filename_ptr;

  tmp = (char*) malloc(strlen(filename) * 2 + 1);
  if (tmp == NULL) {
    errormsg("out of memory!\n");
    exit(1);
  }

  for (x = 0, tx = 0; x < strlen(filename); x++) {
    if (strchr(escape_list, filename[x]) != NULL) tmp[tx++] = '\\';
    tmp[tx++] = filename[x];
  }

  tmp[tx] = '\0';

  if (x != tx) {
    *filename_ptr = realloc(*filename_ptr, strlen(tmp) + 1);
    if (*filename_ptr == NULL) {
      errormsg("out of memory!\n");
      exit(1);
    }
    strcpy(*filename_ptr, tmp);
  }
}

static char **cloneargs(const int argc, char **argv)
{
  unsigned int x;
  char **args;

  args = (char **) malloc(sizeof(char*) * argc);
  if (args == NULL) goto oom;

  for (x = 0; x < argc; x++) {
    args[x] = (char*) malloc(strlen(argv[x]) + 1);
    if (args[x] == NULL) goto oom;
    strcpy(args[x], argv[x]);
  }

  return args;

oom:
  errormsg("out of memory!\n");
  exit(1);
}

static int findarg(const char * const arg, const int start,
		const int argc, char **argv)
{
  unsigned int x;

  for (x = start; x < argc; x++)
    if (strcmp(argv[x], arg) == 0)
      return x;

  return x;
}

/* Find the first non-option argument after specified option. */
static int nonoptafter(const char *option, const int argc,
		char **oldargv, char **newargv, int optind)
{
  int x;
  int targetind;
  int testind;
  int startat = 1;

  targetind = findarg(option, 1, argc, oldargv);

  for (x = optind; x < argc; x++) {
    testind = findarg(newargv[x], startat, argc, oldargv);
    if (testind > targetind) return x;
    else startat = testind;
  }

  return x;
}

static int grokdir(const char *dir, file_t ** const filelistp)
{
  DIR *cd;
  file_t *newfile;
  struct dirent *dirinfo;
  int lastchar;
  int filecount = 0;
  struct stat info;
#ifndef NO_SYMLINKS
  struct stat linfo;
#endif
  static int progress = 0;
  static int delay = DELAY_COUNT;
  static char indicator[] = "-\\|/";
  char *fullname, *name;

  cd = opendir(dir);

  if (!cd) {
    errormsg("could not chdir to %s\n", dir);
    return 0;
  }

  while ((dirinfo = readdir(cd)) != NULL) {
    if (strcmp(dirinfo->d_name, ".") && strcmp(dirinfo->d_name, "..")) {
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        if (delay >= DELAY_COUNT) {
          delay = 0;
          fprintf(stderr, "\rBuilding file list %c ", indicator[progress]);
          progress = (progress + 1) % 4;
        } else delay++;
      }

      newfile = (file_t*) malloc(sizeof(file_t));
      if (!newfile) goto oom;
      else newfile->next = *filelistp;

      newfile->device = 0;
      newfile->inode = 0;
      newfile->crcsignature_set = 0;
      newfile->crcsignature = 0;
      newfile->crcpartial_set = 0;
      newfile->crcpartial = 0;
      newfile->duplicates = NULL;
      newfile->hasdupes = 0;

      newfile->d_name = (char*)malloc(strlen(dir)+strlen(dirinfo->d_name)+2);
      if (!newfile->d_name) goto oom;

      strcpy(newfile->d_name, dir);
      lastchar = strlen(dir) - 1;
      if (lastchar >= 0 && dir[lastchar] != '/')
        strcat(newfile->d_name, "/");
      strcat(newfile->d_name, dirinfo->d_name);

      if (ISFLAG(flags, F_EXCLUDEHIDDEN)) {
        fullname = strdup(newfile->d_name);
        name = basename(fullname);
        if (name[0] == '.' && strcmp(name, ".") && strcmp(name, "..")) {
          free(newfile->d_name);
          free(newfile);
          continue;
        }
        free(fullname);
      }

      if (stat(newfile->d_name, &info) == -1) {
	free(newfile->d_name);
	free(newfile);
	continue;
      }

      if (!S_ISDIR(info.st_mode) && info.st_size == 0 && ISFLAG(flags, F_EXCLUDEEMPTY)) {
	free(newfile->d_name);
	free(newfile);
	continue;
      }

      if (!S_ISDIR(info.st_mode) && ISFLAG(flags, F_EXCLUDESIZE) && info.st_size < excludesize) {
	free(newfile->d_name);
	free(newfile);
	continue;
      }

#ifndef NO_SYMLINKS
      if (lstat(newfile->d_name, &linfo) == -1) {
	free(newfile->d_name);
	free(newfile);
	continue;
      }
#endif

      if (S_ISDIR(info.st_mode)) {
#ifndef NO_SYMLINKS
	if (ISFLAG(flags, F_RECURSE) && (ISFLAG(flags, F_FOLLOWLINKS) || !S_ISLNK(linfo.st_mode)))
          filecount += grokdir(newfile->d_name, filelistp);
#else
	if (ISFLAG(flags, F_RECURSE))
          filecount += grokdir(newfile->d_name, filelistp);
#endif
	free(newfile->d_name);
	free(newfile);
      } else {
#ifndef NO_SYMLINKS
        if (S_ISREG(linfo.st_mode) || (S_ISLNK(linfo.st_mode) && ISFLAG(flags, F_FOLLOWLINKS))) {
#else
        if (S_ISREG(info.st_mode)) {
#endif
          newfile->size = info.st_size;
	  *filelistp = newfile;
	  filecount++;
	} else {
	  free(newfile->d_name);
	  free(newfile);
	}
      }
    }
  }

  closedir(cd);

  return filecount;

oom:
  errormsg("out of memory!\n");
  exit(1);
}

/* Use Jody Bruchon's hash function on part or all of a file */
static hash_t *getcrcsignatureuntil(const char * const filename,
		const off_t max_read)
{
  off_t fsize;
  off_t toread;
  static hash_t hash[1];
  char chunk[CHUNK_SIZE];
  FILE *file;
  struct stat s;

  if (stat(filename, &s) == -1) return NULL;
  fsize = s.st_size;

  if (max_read != 0 && fsize > max_read)
    fsize = max_read;

  file = fopen(filename, "rb");
  if (file == NULL) {
    errormsg("error opening file %s\n", filename);
    return NULL;
  }

  while (fsize > 0) {
    toread = (fsize >= CHUNK_SIZE) ? CHUNK_SIZE : fsize;
    if (fread(chunk, toread, 1, file) != 1) {
      errormsg("error reading from file %s\n", filename);
      fclose(file);
      return NULL;
    }

    *hash = 0;
    *hash = jody_block_hash((hash_t *)chunk, *hash, toread);
    if (toread > fsize) fsize = 0;
    else fsize -= toread;
  }

  fclose(file);

  return hash;
}

static inline void purgetree(filetree_t *checktree)
{
  if (checktree->left != NULL) purgetree(checktree->left);
  if (checktree->right != NULL) purgetree(checktree->right);
  free(checktree);
}

static inline void getfilestats(file_t * const file)
{
  struct stat s;

  if (stat(file->d_name, &s) != 0) {
    file->size = -1;
    file->inode = 0;
    file->device = 0;
    file->mtime = 0;
    return;
  }
  file->size = s.st_size;
  file->inode = s.st_ino;
  file->device = s.st_dev;
  file->mtime = s.st_mtime;
  return;
}

static int registerfile(filetree_t **branch, file_t *file)
{
  getfilestats(file);

  *branch = (filetree_t*) malloc(sizeof(filetree_t));
  if (*branch == NULL) {
    errormsg("out of memory!\n");
    exit(1);
  }

  (*branch)->file = file;
  (*branch)->left = NULL;
  (*branch)->right = NULL;

  return 1;
}

static int same_permissions(char* name1, char* name2)
{
    struct stat s1, s2;

    if (stat(name1, &s1) != 0) return -1;
    if (stat(name2, &s2) != 0) return -1;

    return (s1.st_mode == s2.st_mode &&
            s1.st_uid == s2.st_uid &&
            s1.st_gid == s2.st_gid);
}

static file_t **checkmatch(filetree_t *checktree, file_t *file)
{
  int cmpresult;
  hash_t *crcsignature;
  off_t fsize;
  struct stat s;

  /* If device and inode fields are equal one of the files is a
     hard link to the other or the files have been listed twice
     unintentionally. We don't want to flag these files as
     duplicates unless the user specifies otherwise.
  */

  if (stat(file->d_name, &s)) {
    s.st_size = -1;
    s.st_ino = 0;
    s.st_dev = 0;
  }
  /* Hard link checks always fail when building for Windows */
#ifndef NO_HARDLINKS
  if (!ISFLAG(flags, F_CONSIDERHARDLINKS)) {
    if ((s.st_ino ==
        checktree->file->inode) && (s.st_dev ==
        checktree->file->device)) return NULL;
  }
#endif

  fsize = s.st_size;

  /* Exclude files that are not the same size */
  if (fsize < checktree->file->size) cmpresult = -1;
  else if (fsize > checktree->file->size) cmpresult = 1;
  /* Exclude files by permissions if requested */
  else if (ISFLAG(flags, F_PERMISSIONS) &&
        !same_permissions(file->d_name, checktree->file->d_name))
        cmpresult = -1;
  else {
    /* Attempt to exclude files quickly with partial file hashing */
    partial_hash++;
    if (checktree->file->crcpartial_set == 0) {
      crcsignature = getcrcpartialsignature(checktree->file->d_name);
      if (crcsignature == NULL) {
        errormsg ("cannot read file %s\n", checktree->file->d_name);
        return NULL;
      }

      checktree->file->crcpartial = *crcsignature;
      checktree->file->crcpartial_set = 1;
    }

    if (file->crcpartial_set == 0) {
      crcsignature = getcrcpartialsignature(file->d_name);
      if (crcsignature == NULL) {
        errormsg ("cannot read file %s\n", file->d_name);
        return NULL;
      }

      file->crcpartial = *crcsignature;
      file->crcpartial_set = 1;
    }

    cmpresult = crc_cmp(file->crcpartial, checktree->file->crcpartial);

    if (file->size <= PARTIAL_HASH_SIZE) {
      /* crcpartial = crcsignature if file is small enough */
      if (file->crcsignature_set == 0) {
        file->crcsignature = file->crcpartial;
        file->crcsignature_set = 1;
        small_file++;
      }
      if (checktree->file->crcsignature_set == 0) {
        checktree->file->crcsignature = checktree->file->crcpartial;
        checktree->file->crcsignature_set = 1;
        small_file++;
      }
    } else if (cmpresult == 0) {
      /* If partial match was correct, perform a full file hash match */
      if (checktree->file->crcsignature_set == 0) {
	crcsignature = getcrcsignature(checktree->file->d_name);
	if (crcsignature == NULL) return NULL;

	checktree->file->crcsignature = *crcsignature;
        checktree->file->crcsignature_set = 1;
      }

      if (file->crcsignature_set == 0) {
	crcsignature = getcrcsignature(file->d_name);
	if (crcsignature == NULL) return NULL;

	file->crcsignature = *crcsignature;
	file->crcsignature_set = 1;
      }

      cmpresult = crc_cmp(file->crcsignature, checktree->file->crcsignature);

      /*if (cmpresult != 0) errormsg("P   on %s vs %s\n",
          file->d_name, checktree->file->d_name);
      else errormsg("P F on %s vs %s\n", file->d_name,
          checktree->file->d_name);
      printf("%s matches %s\n", file->d_name, checktree->file->d_name);*/
    }
  }

  if (cmpresult < 0) {
    if (checktree->left != NULL) {
      return checkmatch(checktree->left, file);
    } else {
      registerfile(&(checktree->left), file);
      return NULL;
    }
  } else if (cmpresult > 0) {
    if (checktree->right != NULL) {
      return checkmatch(checktree->right, file);
    } else {
      registerfile(&(checktree->right), file);
      return NULL;
    }
  } else {
    /* All compares matched */
    partial_to_full++;
    getfilestats(file);
    return &checktree->file;
  }
  /* Fall through - should never be reached */
  return NULL;
}

/* Do a bit-for-bit comparison in case two different files produce the
   same signature. Unlikely, but better safe than sorry. */

static inline int confirmmatch(FILE * const file1, FILE * const file2)
{
  unsigned char c1[CHUNK_SIZE];
  unsigned char c2[CHUNK_SIZE];
  size_t r1;
  size_t r2;

  fseek(file1, 0, SEEK_SET);
  fseek(file2, 0, SEEK_SET);

  do {
    r1 = fread(c1, sizeof(unsigned char), sizeof(c1), file1);
    r2 = fread(c2, sizeof(unsigned char), sizeof(c2), file2);

    if (r1 != r2) return 0; /* file lengths are different */
    if (memcmp (c1, c2, r1)) return 0; /* file contents are different */
  } while (r2);

  return 1;
}

static void summarizematches(file_t *files)
{
  int numsets = 0;
  double numbytes = 0.0;
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
    if (numbytes < 1024.0)
      printf("%d duplicate files (in %d sets), occupying %.0f bytes.\n", numfiles, numsets, numbytes);
    else if (numbytes <= (1000.0 * 1000.0))
      printf("%d duplicate files (in %d sets), occupying %.1f kilobytes\n", numfiles, numsets, numbytes / 1000.0);
    else
      printf("%d duplicate files (in %d sets), occupying %.1f megabytes\n", numfiles, numsets, numbytes / (1000.0 * 1000.0));

  }
}

static void printmatches(file_t *files)
{
  file_t *tmpfile;

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
  int x;
  int i;

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

  if (!dupelist || !preserve || !preservestr) {
    errormsg("out of memory\n");
    exit(1);
  }

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
	printf("Set %d of %d, preserve files [1 - %d, all]",
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
	  if (!tstr) { /* couldn't allocate memory, treat as fatal */
	    errormsg("out of memory!\n");
	    exit(1);
	  }

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
	  if (strncasecmp(token, "all", 4) == 0)
	    for (x = 0; x <= counter; x++) preserve[x] = 1;

	  number = 0;
	  sscanf(token, "%d", &number);
	  if (number > 0 && number <= counter) preserve[number] = 1;

	  token = strtok(NULL, " ,\n");
	}

	for (sum = 0, x = 1; x <= counter; x++) sum += preserve[x];
      } while (sum < 1); /* make sure we've preserved at least one file */

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

static inline int sort_pairs_by_arrival(file_t *f1, file_t *f2)
{
  if (f2->duplicates != 0) return 1;

  return -1;
}

static inline int sort_pairs_by_mtime(file_t *f1, file_t *f2)
{
  if (f1->mtime < f2->mtime) return -1;
  else if (f1->mtime > f2->mtime) return 1;

  return 0;
}

#define IS_NUM(a) (((a >= '0') && (a <= '9')) ? 1 : 0)
static inline int numeric_sort(char *c1, char *c2)
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

static inline int sort_pairs_by_filename(file_t *f1, file_t *f2)
{
	return numeric_sort(f1->d_name, f2->d_name);
}

static void registerpair(file_t **matchlist, file_t *newmatch,
		  int (*comparef)(file_t *f1, file_t *f2))
{
  file_t *traverse;
  file_t *back;

  (*matchlist)->hasdupes = 1;
  back = 0;
  traverse = *matchlist;

  while (traverse) {
    if (comparef(newmatch, traverse) <= 0) {
      newmatch->duplicates = traverse;

      if (back == 0) {
	*matchlist = newmatch; /* update pointer to head of list */
	newmatch->hasdupes = 1;
	traverse->hasdupes = 0; /* flag is only for first file in dupe chain */
      } else back->duplicates = newmatch;

      break;
    } else {
      if (traverse->duplicates == 0) {
	traverse->duplicates = newmatch;
	if (back == 0) traverse->hasdupes = 1;

	break;
      }
    }

    back = traverse;
    traverse = traverse->duplicates;
  }
}

#ifndef NO_HARDLINKS
static void hardlinkfiles(file_t *files)
{
  int counter;
  int groups = 0;
  int curgroup = 0;
  file_t *tmpfile;
  file_t *curfile;
  file_t **dupelist;
  int max = 0;
  int x = 0;

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

  if (!dupelist) {
    errormsg("out of memory\n");
    exit(1);
  }

  while (files) {
    if (files->hasdupes) {
      curgroup++;
      counter = 1;
      dupelist[counter] = files;

      tmpfile = files->duplicates;

      while (tmpfile) {
       dupelist[++counter] = tmpfile;
       tmpfile = tmpfile->duplicates;
      }

      /* preserve only the first file */

      if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("   [+] %s\n", dupelist[1]->d_name);
      for (x = 2; x <= counter; x++) {
         if (unlink(dupelist[x]->d_name) == 0) {
            if ( link(dupelist[1]->d_name, dupelist[x]->d_name) == 0) {
              if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("   [h] %s\n", dupelist[x]->d_name);
            } else {
              if (!ISFLAG(flags, F_HIDEPROGRESS)) {
                printf("-- unable to create a hard link for the file: %s\n", strerror(errno));
                printf("   [!] %s ", dupelist[x]->d_name);
	      }
            }
         } else if (!ISFLAG(flags, F_HIDEPROGRESS)) {
           printf("   [!] %s ", dupelist[x]->d_name);
           printf("-- unable to delete the file!\n");
         }
       }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("\n");
    }

    files = files->next;
  }

  free(dupelist);
}
#endif /* NO_HARDLINKS */


static void help_text()
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
  printf("                  \tSIZE argument accepts 'k', 'M' and 'G' unit suffix\n");
  printf(" -A --nohidden    \texclude hidden files from consideration\n");
  printf(" -f --omitfirst   \tomit the first file in each set of matches\n");
  printf(" -1 --sameline    \tlist each set of matches on a single line\n");
  printf(" -S --size        \tshow size of duplicate files\n");
  printf(" -m --summarize   \tsummarize dupe information\n");
  printf(" -q --quiet       \thide progress indicator\n");
  printf(" -d --delete      \tprompt user for files to preserve and delete all\n");
  printf("                  \tothers; important: under particular circumstances,\n");
  printf("                  \tdata may be lost when using this option together\n");
  printf("                  \twith -s or --symlinks, or when specifying a\n");
  printf("                  \tparticular directory more than once; refer to the\n");
  printf("                  \tfdupes documentation for additional information\n");
  printf(" -N --noprompt    \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
  printf(" -p --permissions \tdon't consider files with different owner/group or\n");
  printf("                  \tpermission bits as duplicates\n");
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
  printf("                  \tmtime (BY='time'; default) or filename (BY='name')\n");
  printf(" -v --version     \tdisplay fdupes version\n");
  printf(" -h --help        \tdisplay this help message\n\n");
#ifdef OMIT_GETOPT_LONG
  printf("Note: Long options are not supported in this fdupes build.\n\n");
#endif
}

int main(int argc, char **argv) {
  int x;
  int opt;
  FILE *file1;
  FILE *file2;
  file_t *files = NULL;
  file_t *curfile;
  file_t **match = NULL;
  filetree_t *checktree = NULL;
  int filecount = 0;
  int progress = 0;
  char **oldargv;
  int firstrecurse;
  ordertype_t ordertype = ORDER_TIME;
  int delay = DELAY_COUNT;
  char *endptr;

#ifndef OMIT_GETOPT_LONG
  static struct option long_options[] =
  {
    { "omitfirst", 0, 0, 'f' },
    { "recurse", 0, 0, 'r' },
    { "recursive", 0, 0, 'r' },
    { "recurse:", 0, 0, 'R' },
    { "recursive:", 0, 0, 'R' },
    { "quiet", 0, 0, 'q' },
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
    { "version", 0, 0, 'v' },
    { "help", 0, 0, 'h' },
    { "noprompt", 0, 0, 'N' },
    { "summarize", 0, 0, 'm'},
    { "summary", 0, 0, 'm' },
    { "permissions", 0, 0, 'p' },
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
#ifndef ON_WINDOWS
  "frRq1SsHLnx:AdvhNmpo:"
#else
  #ifdef NO_SYMLINKS
  "frRq1Snx:AdvhNmpo:"
  #else
  "frRq1Ssnx:AdvhNmpo:"
  #endif /* NO_SYMLINKS */
#endif /* ON_WINDOWS */
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
          excludesize = excludesize * 1024;
          endptr++;
          break;
        case 'M':
          excludesize = excludesize * 1024 * 1024;
          endptr++;
          break;
        case 'G':
          excludesize = excludesize * 1024 * 1024 * 1024;
          endptr++;
          break;
        default:
          break;
      }
      if (*endptr != '\0') {
        errormsg("invalid value for --xsize: '%s'\n", optarg);
        exit(1);
      }
      break;
    case 'A':
      SETFLAG(flags, F_EXCLUDEHIDDEN);
      break;
    case 'd':
      SETFLAG(flags, F_DELETEFILES);
      break;
    case 'v':
      printf("fdupes %s\n", VERSION);
      exit(0);
    case 'h':
      help_text();
      exit(1);
    case 'N':
      SETFLAG(flags, F_NOPROMPT);
      break;
    case 'm':
      SETFLAG(flags, F_SUMMARIZEMATCHES);
      break;
    case 'p':
      SETFLAG(flags, F_PERMISSIONS);
      break;
    case 'o':
      if (!strncasecmp("name", optarg, 5)) {
        ordertype = ORDER_NAME;
      } else if (!strncasecmp("time", optarg, 5)) {
        ordertype = ORDER_TIME;
      } else {
        errormsg("invalid value for --order: '%s'\n", optarg);
        exit(1);
      }
      break;

    default:
      fprintf(stderr, "Try `fdupes --help' for more information.\n");
      exit(1);
    }
  }

  if (optind >= argc) {
    errormsg("no directories specified\n");
    exit(1);
  }
#ifndef NO_HARDLINKS
  if (ISFLAG(flags, F_HARDLINKFILES) && ISFLAG(flags, F_DELETEFILES)) {
    errormsg("options --linkhard and --delete are not compatible\n");
    exit(1);
  }

  if (ISFLAG(flags, F_HARDLINKFILES) && ISFLAG(flags, F_CONSIDERHARDLINKS)) {
    errormsg("options --linkhard and --hardlinks are not compatible\n");
    exit(1);
  }
#endif	/* NO_HARDLINKS */
  if (ISFLAG(flags, F_RECURSE) && ISFLAG(flags, F_RECURSEAFTER)) {
    errormsg("options --recurse and --recurse: are not compatible\n");
    exit(1);
  }

  if (ISFLAG(flags, F_SUMMARIZEMATCHES) && ISFLAG(flags, F_DELETEFILES)) {
    errormsg("options --summarize and --delete are not compatible\n");
    exit(1);
  }

  if (ISFLAG(flags, F_RECURSEAFTER)) {
    firstrecurse = nonoptafter("--recurse:", argc, oldargv, argv, optind);

    if (firstrecurse == argc)
      firstrecurse = nonoptafter("-R", argc, oldargv, argv, optind);

    if (firstrecurse == argc) {
      errormsg("-R option must be isolated from other options\n");
      exit(1);
    }

    /* F_RECURSE is not set for directories before --recurse: */
    for (x = optind; x < firstrecurse; x++)
      filecount += grokdir(argv[x], &files);

    /* Set F_RECURSE for directories after --recurse: */
    SETFLAG(flags, F_RECURSE);

    for (x = firstrecurse; x < argc; x++)
      filecount += grokdir(argv[x], &files);
  } else {
    for (x = optind; x < argc; x++)
      filecount += grokdir(argv[x], &files);
  }

  if (!files) {
    if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r%40s\r", " ");
    exit(0);
  }

  curfile = files;

  while (curfile) {
    if (!checktree) registerfile(&checktree, curfile);
    else match = checkmatch(checktree, curfile);

    /* Byte-for-byte check that a matched pair are actually matched */
    if (match != NULL) {
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
      } else hash_fail++;

      fclose(file1);
      fclose(file2);
    }

    curfile = curfile->next;

    if (!ISFLAG(flags, F_HIDEPROGRESS)) {
      /* If file size is larger than 1 MiB, make progress update faster */
      if (curfile != NULL) delay += (curfile->size >> 20);
      if ((delay >= DELAY_COUNT)) {
        delay = 0;
        fprintf(stderr, "\rProgress [%d/%d] %d%% ", progress, filecount,
          (int)((progress * 100) / filecount));
      } else delay++;
      progress++;
    }
  }

  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r%40s\r", " ");

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

  while (files) {
    curfile = files->next;
    free(files->d_name);
    free(files);
    files = curfile;
  }

  for (x = 0; x < argc; x++)
    free(oldargv[x]);

  free(oldargv);
  purgetree(checktree);

  /* Uncomment this to see hash statistics after running the program
  fprintf(stderr, "\n%d partial (+%d small) -> %d full (%d partial elim) (%d hash fail)\n",
		  partial_hash, small_file, partial_to_full,
		  (partial_hash - partial_to_full), hash_fail);
  */

  exit(0);

}
