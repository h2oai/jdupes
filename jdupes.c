/* jdupes (C) 2015-2016 Jody Bruchon <jody@jodybruchon.com>
   Derived from fdupes (C) 1999-2016 Adrian Lopez

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
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#ifndef OMIT_GETOPT_LONG
 #include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include "string_malloc.h"
#include "jody_hash.h"
#include "jody_sort.h"
#include "version.h"

/* Optional btrfs support */
#ifdef ENABLE_BTRFS
#include <sys/ioctl.h>
#include <linux/btrfs.h>
#endif

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __CYGWIN__
 #define ON_WINDOWS 1
 #define NO_SYMLINKS 1
 #define NO_PERMS 1
 #define NO_SIGACTION 1
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <io.h>
 #include "win_stat.h"
 #define S_ISREG WS_ISREG
 #define S_ISDIR WS_ISDIR
 typedef uint64_t jdupes_ino_t;
 typedef uint32_t jdupes_mode_t;
 static const char dir_sep = '\\';
 #ifdef UNICODE
  static const wchar_t *FILE_MODE_RO = L"rbS";
 #else
  static const char *FILE_MODE_RO = "rbS";
 #endif /* UNICODE */

#else /* Not Windows */
 #include <sys/stat.h>
 typedef ino_t jdupes_ino_t;
 typedef mode_t jdupes_mode_t;
 static const char *FILE_MODE_RO = "rb";
 static const char dir_sep = '/';
 #ifdef UNICODE
  #error Do not define UNICODE on non-Windows platforms.
  #undef UNICODE
 #endif
#endif /* _WIN32 || __CYGWIN__ */

/* Windows + Unicode compilation */
#ifdef UNICODE
static wchar_t wname[PATH_MAX];
static wchar_t wname2[PATH_MAX];
static wchar_t wstr[PATH_MAX];
static int out_mode = _O_TEXT;
 #define M2W(a,b) MultiByteToWideChar(CP_UTF8, 0, a, -1, (LPWSTR)b, PATH_MAX)
 #define W2M(a,b) WideCharToMultiByte(CP_UTF8, 0, a, -1, (LPSTR)b, PATH_MAX, NULL, NULL)
#endif /* UNICODE */

#ifndef NO_SYMLINKS
#include "jody_paths.h"
#endif

#define ISFLAG(a,b) ((a & b) == b)
#define SETFLAG(a,b) (a |= b)
#define CLEARFLAG(a,b) (a &= (~b))

/* Low memory option overrides */
#ifdef LOW_MEMORY
 #undef DEBUG
 #undef LOUD_DEBUG
 #undef USE_TREE_REBALANCE
 #ifndef NO_PERMS
  #define NO_PERMS 1
 #endif
#endif

/* Aggressive verbosity for deep debugging */
#ifdef LOUD_DEBUG
 #ifndef DEBUG
  #define DEBUG
 #endif
 #define LOUD(...) if ISFLAG(flags, F_LOUD) __VA_ARGS__
#else
 #define LOUD(a)
#endif

/* Compile out debugging stat counters unless requested */
#ifdef DEBUG
 #define DBG(a) a
 #ifndef TREE_DEPTH_STATS
  #define TREE_DEPTH_STATS
 #endif
#else
 #define DBG(a)
#endif


/* How many operations to wait before updating progress counters */
#define DELAY_COUNT 256

/* Behavior modification flags */
static uint_fast32_t flags = 0;
#define F_RECURSE		0x00000001U
#define F_HIDEPROGRESS		0x00000002U
#define F_SOFTABORT		0x00000004U
#define F_FOLLOWLINKS		0x00000008U
#define F_DELETEFILES		0x00000010U
#define F_EXCLUDEEMPTY		0x00000020U
#define F_CONSIDERHARDLINKS	0x00000040U
#define F_SHOWSIZE		0x00000080U
#define F_OMITFIRST		0x00000100U
#define F_RECURSEAFTER		0x00000200U
#define F_NOPROMPT		0x00000400U
#define F_SUMMARIZEMATCHES	0x00000800U
#define F_EXCLUDEHIDDEN		0x00001000U
#define F_PERMISSIONS		0x00002000U
#define F_HARDLINKFILES		0x00004000U
#define F_EXCLUDESIZE		0x00008000U
#define F_QUICKCOMPARE		0x00010000U
#define F_USEPARAMORDER		0x00020000U
#define F_DEDUPEFILES		0x00040000U
#define F_REVERSESORT		0x00080000U
#define F_ISOLATE		0x00100000U
#define F_MAKESYMLINKS		0x00200000U
#define F_PRINTMATCHES		0x00400000U

#define F_LOUD			0x40000000U
#define F_DEBUG			0x80000000U

/* Per-file true/false flags */
#define F_VALID_STAT		0x00000001U
#define F_HASH_PARTIAL		0x00000002U
#define F_HASH_FULL		0x00000004U
#define F_HAS_DUPES		0x00000008U
#define F_IS_SYMLINK		0x00000010U

typedef enum {
  ORDER_NAME = 0,
  ORDER_TIME
} ordertype_t;

static const char *program_name;

/* This gets used in many functions */
#ifdef ON_WINDOWS
static struct winstat ws;
#else
static struct stat s;
#endif

static uintmax_t excludesize = 0;
static enum {
  SMALLERTHAN,
  LARGERTHAN
} excludetype = SMALLERTHAN;

/* Larger chunk size makes large files process faster but uses more RAM */
#ifndef CHUNK_SIZE
 #define CHUNK_SIZE 65536
#endif
#ifndef PARTIAL_HASH_SIZE
 #define PARTIAL_HASH_SIZE 4096
#endif

/* Maximum path buffer size to use; must be large enough for a path plus
 * any work that might be done to the array it's stored in. PATH_MAX is
 * not always true. Read this article on the false promises of PATH_MAX:
 * http://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html
 * Windows + Unicode needs a lot more space than UTF-8 in Linux/Mac OS X
 */
#ifndef PATHBUF_SIZE
#define PATHBUF_SIZE 4096
#endif

/* For interactive deletion input */
#define INPUT_SIZE 512

/* Assemble extension string from compile-time options */
static const char *extensions[] = {
  #ifdef ON_WINDOWS
    "windows",
    #endif
    #ifdef UNICODE
    "unicode",
    #endif
    #ifdef OMIT_GETOPT_LONG
    "nolong",
    #endif
    #ifdef __FAST_MATH__
    "fastmath",
    #endif
    #ifdef DEBUG
    "debug",
    #endif
    #ifdef LOUD_DEBUG
    "loud",
    #endif
    #ifdef ENABLE_BTRFS
    "btrfs",
    #endif
    #ifdef LOW_MEMORY
    "lowmem",
    #endif
    #ifdef SMA_PAGE_SIZE
    "smapage",
    #endif
    #if JODY_HASH_WIDTH == 32
    "hash32",
    #endif
    #if JODY_HASH_WIDTH == 16
    "hash16",
    #endif
    #ifdef NO_PERMS
    "noperm",
    #endif
    #ifdef NO_SYMLINKS
    "nosymlink",
    #endif
    #ifdef USE_TREE_REBALANCE
    "rebal",
    #endif
    #ifdef CONSIDER_IMBALANCE
    "ci",
    #endif
    #ifdef BALANCE_THRESHOLD
    "bt",
    #endif
    NULL
};

/* TODO: Cachegrind indicates that size, inode, and device get hammered hard
 * in the checkmatch() code and trigger lots of cache line evictions.
 * Maybe we can compact these into a separate structure to improve speed. */
typedef struct _file {
  struct _file *duplicates;
  struct _file *next;
  char *d_name;
  dev_t device;
  jdupes_mode_t mode;
  off_t size;
  jdupes_ino_t inode;
  hash_t filehash_partial;
  hash_t filehash;
  time_t mtime;
  uint32_t flags;  /* Status flags */
  unsigned int user_order; /* Order of the originating command-line parameter */
#ifndef NO_PERMS
  uid_t uid;
  gid_t gid;
#endif
#ifdef ON_WINDOWS
 #ifndef NO_HARDLINKS
  DWORD nlink;
 #endif
#endif
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
 #define TREE_DEPTH_STATS
 #ifndef INITIAL_DEPTH_THRESHOLD
  #define INITIAL_DEPTH_THRESHOLD 8
 #endif
static filetree_t *checktree = NULL;
#endif

static uintmax_t filecount = 0; // Required for progress indicator code
static int did_long_work = 0; // To tell progress indicator to go faster

/* Hash/compare performance statistics (debug mode) */
#ifdef DEBUG
static unsigned int small_file = 0, partial_hash = 0, partial_elim = 0;
static unsigned int full_hash = 0, partial_to_full = 0, hash_fail = 0;
static uintmax_t comparisons = 0;
static unsigned int left_branch = 0, right_branch = 0;
 #ifdef ON_WINDOWS
  #ifndef NO_HARDLINKS
static unsigned int hll_exclude = 0;
  #endif
 #endif
#endif /* DEBUG */

#ifdef TREE_DEPTH_STATS
static unsigned int tree_depth = 0, max_depth = 0;
#endif

/* Directory parameter position counter */
static unsigned int user_dir_count = 1;

/* registerfile() direction options */
enum tree_direction { NONE, LEFT, RIGHT };

/* Sort order reversal */
static int sort_direction = 1;

/* Signal handler */
static int interrupt = 0;

/* Message to append to BTRFS warnings based on write permissions */
#ifdef ENABLE_BTRFS
static const char *readonly_msg[] = {
        "", " (no write permission)"
};
#endif


/***** End definitions, begin code *****/


/* Catch CTRL-C and either notify or terminate */
void sighandler(const int signum)
{
  (void)signum;
  if (interrupt || !ISFLAG(flags, F_SOFTABORT)) {
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
  }
  interrupt = 1;
  return;
}


/* Out of memory */
static void oom(const char * const restrict msg)
{
  fprintf(stderr, "\nout of memory: %s\n", msg);
  exit(EXIT_FAILURE);
}


#ifdef UNICODE
/* Convert slashes to backslashes in a file path */
static void slash_convert(char *path)
{
  while (*path != '\0') {
    if (*path == '/') *path = '\\';
    path++;
  }
  return;
}


/* Copy Windows wide character arguments to UTF-8 */
static void widearg_to_argv(int argc, wchar_t **wargv, char **argv)
{
  static char temp[PATH_MAX];
  int len;

  if (!argv) goto error_bad_argv;
  for (int counter = 0; counter < argc; counter++) {
    len = W2M(wargv[counter], &temp);
    if (len < 1) goto error_wc2mb;

    argv[counter] = (char *)string_malloc((size_t)len + 1);
    if (!argv[counter]) oom("widearg_to_argv()");
    strncpy(argv[counter], temp, (size_t)len + 1);
  }
  return;

error_bad_argv:
  fprintf(stderr, "fatal: bad argv pointer\n");
  exit(EXIT_FAILURE);
error_wc2mb:
  fprintf(stderr, "fatal: WideCharToMultiByte failed\n");
  exit(EXIT_FAILURE);
}


/* Print a string that is wide on Windows but normal on POSIX */
static int fwprint(FILE * const restrict stream, const char * const restrict str, const int cr)
{
  int retval;

  if (out_mode != _O_TEXT) {
    /* Convert to wide string and send to wide console output */
    if (!MultiByteToWideChar(CP_UTF8, 0, str, -1, (LPWSTR)wstr, PATH_MAX)) return -1;
    fflush(stdout); fflush(stderr);
    _setmode(_fileno(stream), out_mode);
    retval = fwprintf(stream, L"%S%S", wstr, cr ? L"\n" : L"");
    fflush(stdout); fflush(stderr);
    _setmode(_fileno(stream), _O_TEXT);
    return retval;
  } else {
    return fprintf(stream, "%s%s", str, cr ? "\n" : "");
  }
}
#else
 #define fwprint(a,b,c) fprintf(a, "%s%s", b, c ? "\n" : "")
 #define slash_convert(a)
#endif /* UNICODE */



/* Compare two jody_hashes like memcmp() */
#define HASH_COMPARE(a,b) ((a > b) ? 1:((a == b) ? 0:-1))


static inline char **cloneargs(const int argc, char **argv)
{
  static int x;
  static char **args;

  args = (char **)string_malloc(sizeof(char *) * (unsigned int)argc);
  if (args == NULL) oom("cloneargs() start");

  for (x = 0; x < argc; x++) {
    args[x] = (char *)string_malloc(strlen(argv[x]) + 1);
    if (args[x] == NULL) oom("cloneargs() loop");
    strcpy(args[x], argv[x]);
  }

  return args;
}


static int findarg(const char * const arg, const int start,
                const int argc, char **argv)
{
  int x;

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


/* Check file's stat() info to make sure nothing has changed
 * Returns 1 if changed, 0 if not changed, negative if error */
static int file_has_changed(file_t * const restrict file)
{
  if (!ISFLAG(file->flags, F_VALID_STAT)) return -66;

#ifdef ON_WINDOWS
  int i;
  if ((i = win_stat(file->d_name, &ws)) != 0) return i;
  if (file->inode != ws.inode) return 1;
  if (file->size != ws.size) return 1;
  if (file->device != ws.device) return 1;
  if (file->mtime != ws.mtime) return 1;
  if (file->mode != ws.mode) return 1;
#else
  if (stat(file->d_name, &s) != 0) return -2;
  if (file->inode != s.st_ino) return 1;
  if (file->size != s.st_size) return 1;
  if (file->device != s.st_dev) return 1;
  if (file->mtime != s.st_mtime) return 1;
  if (file->mode != s.st_mode) return 1;
 #ifndef NO_PERMS
  if (file->uid != s.st_uid) return 1;
  if (file->gid != s.st_gid) return 1;
 #endif
 #ifndef NO_SYMLINKS
  if (lstat(file->d_name, &s) != 0) return -3;
  if ((S_ISLNK(s.st_mode) > 0) ^ ISFLAG(file->flags, F_IS_SYMLINK)) return 1;
 #endif
#endif /* ON_WINDOWS */

  return 0;
}


static inline int getfilestats(file_t * const restrict file)
{
  /* Don't stat the same file more than once */
  if (ISFLAG(file->flags, F_VALID_STAT)) return 0;
  SETFLAG(file->flags, F_VALID_STAT);

#ifdef ON_WINDOWS
  if (win_stat(file->d_name, &ws) != 0) return -1;
  file->inode = ws.inode;
  file->size = ws.size;
  file->device = ws.device;
  file->mtime = ws.mtime;
  file->mode = ws.mode;
 #ifndef NO_HARDLINKS
  file->nlink = ws.nlink;
 #endif /* NO_HARDLINKS */
#else
  if (stat(file->d_name, &s) != 0) return -1;
  file->inode = s.st_ino;
  file->size = s.st_size;
  file->device = s.st_dev;
  file->mtime = s.st_mtime;
  file->mode = s.st_mode;
 #ifndef NO_PERMS
  file->uid = s.st_uid;
  file->gid = s.st_gid;
 #endif
 #ifndef NO_SYMLINKS
  if (lstat(file->d_name, &s) != 0) return -1;
  if (S_ISLNK(s.st_mode) > 0) SETFLAG(file->flags, F_IS_SYMLINK);
 #endif
#endif /* ON_WINDOWS */
  return 0;
}


/* Check a pair of files for match exclusion conditions
 * Returns:
 *  0 if all condition checks pass
 * -1 or 1 on compare result less/more
 * -2 on an absolute exclusion condition met
 *  2 on an absolute match condition met */
static int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2)
{
  /* Null pointer sanity check */
  if (!file1 || !file2) {
    LOUD(fprintf(stderr, "internal error: null pointer passed to check_conditions()\n");)
    exit(EXIT_FAILURE);
  }

  /* Hard link and symlink + '-s' check */
#ifndef NO_HARDLINKS
  if ((file1->inode == file2->inode) && (file1->device == file2->device)) {
    if (ISFLAG(flags, F_CONSIDERHARDLINKS)) {
      LOUD(fprintf(stderr, "check_conditions: files match: hard/soft linked (-H on)\n"));
      return 2;
    } else {
      LOUD(fprintf(stderr, "check_conditions: files ignored: hard/soft linked (-H off)\n"));
      return -2;
    }
  }
#endif

  /* Exclude based on -I/--isolate */
  if (ISFLAG(flags, F_ISOLATE) && (file1->user_order == file2->user_order)) {
    LOUD(fprintf(stderr, "check_conditions: files ignored: parameter isolation\n"));
    return -1;
  }

  /* Exclude files that are not the same size */
  if (file1->size > file2->size) {
    LOUD(fprintf(stderr, "check_conditions: no match: file1 > file2 (%jd < %jd)\n", (intmax_t)file1->size, (intmax_t)file2->size));
    return -1;
  }
  if (file1->size < file2->size) {
    LOUD(fprintf(stderr, "check_conditions: no match: file1 < file2 (%jd > %jd)\n", (intmax_t)file1->size, (intmax_t)file2->size));
    return 1;
  }

  /* Exclude files by permissions if requested */
  if (ISFLAG(flags, F_PERMISSIONS) &&
          (file1->mode != file2->mode
#ifndef NO_PERMS
          || file1->uid != file2->uid
          || file1->gid != file2->gid
#endif
          )) {
    return -1;
    LOUD(fprintf(stderr, "check_conditions: no match: permissions/ownership differ (-p on)\n"));
  }

  /* Fall through: all checks passed */
  LOUD(fprintf(stderr, "check_conditions: all condition checks passed\n"));
  return 0;
}


/* Load a directory's contents into the file tree, recursing as needed */
static void grokdir(const char * const restrict dir,
                file_t * restrict * const restrict filelistp,
                int recurse)
{
  file_t * restrict newfile;
#ifndef NO_SYMLINKS
  static struct stat linfo;
#endif
  struct dirent *dirinfo;
  static uintmax_t progress = 0, dir_progress = 0;
  static int grokdir_level = 0;
  static int delay = DELAY_COUNT;
  static char tempname[PATHBUF_SIZE * 2];
  size_t dirlen;
#ifdef UNICODE
  WIN32_FIND_DATA ffd;
  HANDLE hFind = INVALID_HANDLE_VALUE;
  char *p;
#else
  DIR *cd;
#endif

  LOUD(fprintf(stderr, "grokdir: scanning '%s' (order %d)\n", dir, user_dir_count));

  dir_progress++;
  grokdir_level++;

#ifdef UNICODE
  /* Windows requires \* at the end of directory names */
  strncpy(tempname, dir, PATHBUF_SIZE * 2);
  dirlen = strlen(tempname) - 1;
  p = tempname + dirlen;
  if (*p == '/' || *p == '\\') *p = '\0';
  strncat(tempname, "\\*", PATHBUF_SIZE * 2);

  if (!M2W(tempname, wname)) goto error_cd;

  LOUD(fprintf(stderr, "FindFirstFile: %s\n", dir));
  hFind = FindFirstFile((LPCWSTR)wname, &ffd);
  if (hFind == INVALID_HANDLE_VALUE) { fprintf(stderr, "handle bad\n"); goto error_cd; }
  LOUD(fprintf(stderr, "Loop start\n"));
  do {
    char * restrict tp = tempname;
    size_t d_name_len;

    /* Get necessary length and allocate d_name */
    dirinfo = (struct dirent *)string_malloc(sizeof(struct dirent));
    if (!W2M(ffd.cFileName, dirinfo->d_name)) continue;
#else
  cd = opendir(dir);
  if (!cd) goto error_cd;

  while ((dirinfo = readdir(cd)) != NULL) {
    char * restrict tp = tempname;
    size_t d_name_len;
#endif /* UNICODE */

    LOUD(fprintf(stderr, "grokdir: readdir: '%s'\n", dirinfo->d_name));
    if (strcmp(dirinfo->d_name, ".") && strcmp(dirinfo->d_name, "..")) {
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        if (delay >= DELAY_COUNT) {
          delay = 0;
          fprintf(stderr, "\rScanning: %ju files, %ju dirs (in %u specified)",
              progress, dir_progress, user_dir_count);
        } else delay++;
      }

      /* Assemble the file's full path name, optimized to avoid strcat() */
      dirlen = strlen(dir);
      d_name_len = strlen(dirinfo->d_name);
      memcpy(tp, dir, dirlen+1);
      if (dirlen != 0 && tp[dirlen-1] != dir_sep) {
        tp[dirlen] = dir_sep;
        dirlen++;
      }
      if (dirlen + d_name_len + 1 >= (PATHBUF_SIZE * 2)) goto error_overflow;
      tp += dirlen;
      memcpy(tp, dirinfo->d_name, d_name_len);
      tp += d_name_len;
      *tp = '\0';
      d_name_len++;

      /* Allocate the file_t and the d_name entries */
      newfile = (file_t *)string_malloc(sizeof(file_t));
      if (!newfile) oom("grokdir() file structure");
      newfile->d_name = (char *)string_malloc(dirlen + d_name_len + 2);
      if (!newfile->d_name) oom("grokdir() filename");

      newfile->next = *filelistp;
      newfile->user_order = user_dir_count;
      newfile->size = -1;
      newfile->device = 0;
      newfile->inode = 0;
      newfile->mtime = 0;
      newfile->mode = 0;
#ifdef ON_WINDOWS
 #ifndef NO_HARDLINKS
      newfile->nlink = 0;
 #endif
#endif
#ifndef NO_PERMS
      newfile->uid = 0;
      newfile->gid = 0;
#endif
      newfile->filehash = 0;
      newfile->filehash_partial = 0;
      newfile->duplicates = NULL;
      newfile->flags = 0;

      tp = tempname;
      memcpy(newfile->d_name, tp, dirlen + d_name_len);

      if (ISFLAG(flags, F_EXCLUDEHIDDEN)) {
        /* WARNING: Re-used tp here to eliminate a strdup() */
        strncpy(tp, newfile->d_name, dirlen + d_name_len);
        tp = basename(tp);
        if (tp[0] == '.' && strcmp(tp, ".") && strcmp(tp, "..")) {
          LOUD(fprintf(stderr, "grokdir: excluding hidden file (-A on)\n"));
          string_free(newfile->d_name);
          string_free(newfile);
          continue;
        }
      }

      /* Get file information and check for validity */
      const int i = getfilestats(newfile);
      if (i || newfile->size == -1) {
        LOUD(fprintf(stderr, "grokdir: excluding due to bad stat()\n"));
        string_free(newfile->d_name);
        string_free(newfile);
        continue;
      }

      /* Exclude zero-length files if requested */
      if (!S_ISDIR(newfile->mode) && newfile->size == 0 && ISFLAG(flags, F_EXCLUDEEMPTY)) {
        LOUD(fprintf(stderr, "grokdir: excluding zero-length empty file (-n on)\n"));
        string_free(newfile->d_name);
        string_free(newfile);
        continue;
      }

      /* Exclude files below --xsize parameter */
      if (!S_ISDIR(newfile->mode) && ISFLAG(flags, F_EXCLUDESIZE)) {
        if (
            ((excludetype == SMALLERTHAN) && (newfile->size < (off_t)excludesize)) ||
            ((excludetype == LARGERTHAN) && (newfile->size > (off_t)excludesize))
        ) {
          LOUD(fprintf(stderr, "grokdir: excluding based on xsize limit (-x set)\n"));
          string_free(newfile->d_name);
          string_free(newfile);
          continue;
        }
      }

#ifndef NO_SYMLINKS
      /* Get lstat() information */
      if (lstat(newfile->d_name, &linfo) == -1) {
        LOUD(fprintf(stderr, "grokdir: excluding due to bad lstat()\n"));
        string_free(newfile->d_name);
        string_free(newfile);
        continue;
      }
#endif

      /* Windows has a 1023 (+1) hard link limit. If we're hard linking,
       * ignore all files that have hit this limit */
#ifdef ON_WINDOWS
 #ifndef NO_HARDLINKS
      if (ISFLAG(flags, F_HARDLINKFILES) && newfile->nlink >= 1024) {
  #ifdef DEBUG
        hll_exclude++;
  #endif
        LOUD(fprintf(stderr, "grokdir: excluding due to Windows 1024 hard link limit\n"));
        string_free(newfile->d_name);
        string_free(newfile);
        continue;
      }
 #endif
#endif
      /* Optionally recurse directories, including symlinked ones if requested */
      if (S_ISDIR(newfile->mode)) {
#ifndef NO_SYMLINKS
        if (recurse && (/*ISFLAG(flags, F_FOLLOWLINKS) ||*/ !S_ISLNK(linfo.st_mode))) {
          LOUD(fprintf(stderr, "grokdir: directory: recursing (-r/-R)\n"));
          grokdir(newfile->d_name, filelistp, recurse);
        }
#else
        if (recurse) {
          LOUD(fprintf(stderr, "grokdir: directory: recursing (-r/-R)\n"));
          grokdir(newfile->d_name, filelistp, recurse);
        }
#endif
        LOUD(fprintf(stderr, "grokdir: directory: not recursing\n"));
        string_free(newfile->d_name);
        string_free(newfile);
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
          LOUD(fprintf(stderr, "grokdir: not a regular file: %s\n", newfile->d_name);)
          string_free(newfile->d_name);
          string_free(newfile);
        }
      }
    }
  }
#ifdef UNICODE
  while (FindNextFile(hFind, &ffd) != 0);
  FindClose(hFind);
#else
  closedir(cd);
#endif


  grokdir_level--;
  if (grokdir_level == 0 && !ISFLAG(flags, F_HIDEPROGRESS)) {
    fprintf(stderr, "\rExamining %ju files, %ju dirs (in %u specified)",
            progress, dir_progress, user_dir_count);
  }
  return;

error_cd:
  fprintf(stderr, "error: could not chdir to "); fwprint(stderr, dir, 1);
  return;
error_overflow:
  fprintf(stderr, "\nerror: a path buffer overflowed\n");
  exit(EXIT_FAILURE);
}

/* Use Jody Bruchon's hash function on part or all of a file */
static hash_t *get_filehash(const file_t * const restrict checkfile,
                const size_t max_read)
{
  off_t fsize;
  /* This is an array because we return a pointer to it */
  static hash_t hash[1];
  static hash_t chunk[(CHUNK_SIZE / sizeof(hash_t))];
  FILE *file;

  /* Get the file size. If we can't read it, bail out early */
  if (checkfile->size == -1) {
    LOUD(fprintf(stderr, "get_filehash: not hashing because stat() info is bad\n"));
    return NULL;
  }
  fsize = checkfile->size;

  /* Do not read more than the requested number of bytes */
  if (max_read > 0 && fsize > (off_t)max_read)
    fsize = (off_t)max_read;

  /* Initialize the hash and file read parameters (with filehash_partial skipped)
   *
   * If we already hashed the first chunk of this file, we don't want to
   * wastefully read and hash it again, so skip the first chunk and use
   * the computed hash for that chunk as our starting point.
   *
   * WARNING: We assume max_read is NEVER less than CHUNK_SIZE here! */

  *hash = 0;
  if (ISFLAG(checkfile->flags, F_HASH_PARTIAL)) {
    *hash = checkfile->filehash_partial;
    /* Don't bother going further if max_read is already fulfilled */
    if (max_read != 0 && max_read <= PARTIAL_HASH_SIZE) {
      LOUD(fprintf(stderr, "Partial hash size (%d) >= max_read (%ju), not hashing anymore\n", PARTIAL_HASH_SIZE, (uintmax_t)max_read);)
      return hash;
    }
  }
#ifdef UNICODE
  if (!M2W(checkfile->d_name, wstr)) file = NULL;
  else file = _wfopen(wstr, FILE_MODE_RO);
#else
  file = fopen(checkfile->d_name, FILE_MODE_RO);
#endif
  if (file == NULL) {
    fprintf(stderr, "error opening file "); fwprint(stderr, checkfile->d_name, 1);
    return NULL;
  }
  /* Actually seek past the first chunk if applicable
   * This is part of the filehash_partial skip optimization */
  if (ISFLAG(checkfile->flags, F_HASH_PARTIAL)) {
    if (fseeko(file, PARTIAL_HASH_SIZE, SEEK_SET) == -1) {
      fclose(file);
      fprintf(stderr, "error seeking in file "); fwprint(stderr, checkfile->d_name, 1);
      return NULL;
    }
    fsize -= PARTIAL_HASH_SIZE;
  }
  /* Read the file in CHUNK_SIZE chunks until we've read it all. */
  while (fsize > 0) {
    size_t bytes_to_read;

    if (interrupt) return 0;
    bytes_to_read = (fsize >= CHUNK_SIZE) ? CHUNK_SIZE : (size_t)fsize;
    if (fread((void *)chunk, bytes_to_read, 1, file) != 1) {
      fprintf(stderr, "error reading from file "); fwprint(stderr, checkfile->d_name, 1);
      fclose(file);
      return NULL;
    }

    *hash = jody_block_hash(chunk, *hash, bytes_to_read);
    if ((off_t)bytes_to_read > fsize) break;
    else fsize -= (off_t)bytes_to_read;
  }

  fclose(file);

  LOUD(fprintf(stderr, "get_filehash: returning hash: 0x%016jx\n", (uintmax_t)*hash));
  return hash;
}


static inline void registerfile(filetree_t * restrict * const restrict nodeptr,
                const enum tree_direction d, file_t * const restrict file)
{
  filetree_t * restrict branch;

  /* Allocate and initialize a new node for the file */
  branch = (filetree_t *)string_malloc(sizeof(filetree_t));
  if (branch == NULL) oom("registerfile() branch");
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
      fprintf(stderr, "Internal error: file tree linkage is broken\n");
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
    default:
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


#ifdef TREE_DEPTH_STATS
#define TREE_DEPTH_UPDATE_MAX() { if (max_depth < tree_depth) max_depth = tree_depth; tree_depth = 0; }
#else
#define TREE_DEPTH_UPDATE_MAX()
#endif


/* Check two files for a match */
static file_t **checkmatch(filetree_t * restrict tree, file_t * const restrict file)
{
  int cmpresult = 0;
  const hash_t * restrict filehash;

  LOUD(fprintf(stderr, "checkmatch (\"%s\", \"%s\")\n", tree->file->d_name, file->d_name));

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
    default: break;
  }

  /* If preliminary matching succeeded, move to full file checks */
  if (cmpresult == 0) {
    LOUD(fprintf(stderr, "checkmatch: starting file data comparisons\n"));
    /* Attempt to exclude files quickly with partial file hashing */
    if (!ISFLAG(tree->file->flags, F_HASH_PARTIAL)) {
      filehash = get_filehash(tree->file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) {
        if (!interrupt) fprintf(stderr, "cannot read file "); fwprint(stderr, tree->file->d_name, 1);
        return NULL;
      }

      tree->file->filehash_partial = *filehash;
      SETFLAG(tree->file->flags, F_HASH_PARTIAL);
    }

    if (!ISFLAG(file->flags, F_HASH_PARTIAL)) {
      filehash = get_filehash(file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) {
        if (!interrupt) fprintf(stderr, "cannot read file "); fwprint(stderr, file->d_name, 1);
        return NULL;
      }

      file->filehash_partial = *filehash;
      SETFLAG(file->flags, F_HASH_PARTIAL);
    }

    cmpresult = HASH_COMPARE(file->filehash_partial, tree->file->filehash_partial);
    LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: partial hashes match\n"));
    LOUD(if (cmpresult) fprintf(stderr, "checkmatch: partial hashes do not match\n"));
    DBG(partial_hash++;)

    if (file->size <= PARTIAL_HASH_SIZE) {
      LOUD(fprintf(stderr, "checkmatch: small file: copying partial hash to full hash\n"));
      /* filehash_partial = filehash if file is small enough */
      if (!ISFLAG(file->flags, F_HASH_FULL)) {
        file->filehash = file->filehash_partial;
        SETFLAG(file->flags, F_HASH_FULL);
        DBG(small_file++;)
      }
      if (!ISFLAG(tree->file->flags, F_HASH_FULL)) {
        tree->file->filehash = tree->file->filehash_partial;
        SETFLAG(tree->file->flags, F_HASH_FULL);
        DBG(small_file++;)
      }
    } else if (cmpresult == 0) {
      /* If partial match was correct, perform a full file hash match */
      if (!ISFLAG(tree->file->flags, F_HASH_FULL)) {
        did_long_work = 1;
        filehash = get_filehash(tree->file, 0);
        if (filehash == NULL) {
          if (!interrupt) fprintf(stderr, "cannot read file "); fwprint(stderr, tree->file->d_name, 1);
          return NULL;
        }

        tree->file->filehash = *filehash;
        SETFLAG(tree->file->flags, F_HASH_FULL);
      }

      if (!ISFLAG(file->flags, F_HASH_FULL)) {
        did_long_work = 1;
        filehash = get_filehash(file, 0);
        if (filehash == NULL) {
          if (!interrupt) fprintf(stderr, "cannot read file "); fwprint(stderr, file->d_name, 1);
          return NULL;
        }

        file->filehash = *filehash;
        SETFLAG(file->flags, F_HASH_FULL);
      }

      /* Full file hash comparison */
      cmpresult = HASH_COMPARE(file->filehash, tree->file->filehash);
      LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: full hashes match\n"));
      LOUD(if (cmpresult) fprintf(stderr, "checkmatch: full hashes do not match\n"));
      DBG(full_hash++);
    } else {
      DBG(partial_elim++);
    }
  }

  if (cmpresult < 0) {
    if (tree->left != NULL) {
      LOUD(fprintf(stderr, "checkmatch: recursing tree: left\n"));
      DBG(left_branch++; tree_depth++;)
      return checkmatch(tree->left, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: left\n"));
      registerfile(&tree, LEFT, file);
      TREE_DEPTH_UPDATE_MAX();
      return NULL;
    }
  } else if (cmpresult > 0) {
    if (tree->right != NULL) {
      LOUD(fprintf(stderr, "checkmatch: recursing tree: right\n"));
      DBG(right_branch++; tree_depth++;)
      return checkmatch(tree->right, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: right\n"));
      registerfile(&tree, RIGHT, file);
      TREE_DEPTH_UPDATE_MAX();
      return NULL;
    }
  } else {
    /* All compares matched */
    DBG(partial_to_full++;)
    TREE_DEPTH_UPDATE_MAX();
    LOUD(fprintf(stderr, "checkmatch: files appear to match based on hashes\n"));
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
  size_t r1;
  size_t r2;

  LOUD(fprintf(stderr, "confirmmatch running\n"));

  did_long_work = 1;
  fseek(file1, 0, SEEK_SET);
  fseek(file2, 0, SEEK_SET);

  do {
    if (interrupt) return 0;
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
  off_t numbytes = 0;
  int numfiles = 0;

  while (files != NULL) {
    file_t *tmpfile;

    if (ISFLAG(files->flags, F_HAS_DUPES)) {
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
    if (numbytes < 1000) printf("%jd byte%c\n", (intmax_t)numbytes, (numbytes != 1) ? 's' : ' ');
    else if (numbytes <= 1000000) printf("%jd KB\n", (intmax_t)(numbytes / 1000));
    else printf("%jd MB\n", (intmax_t)(numbytes / 1000000));
  }
  return;
}


static void printmatches(file_t * restrict files)
{
  file_t * restrict tmpfile;

  while (files != NULL) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      if (!ISFLAG(flags, F_OMITFIRST)) {
        if (ISFLAG(flags, F_SHOWSIZE)) printf("%jd byte%c each:\n", (intmax_t)files->size,
         (files->size != 1) ? 's' : ' ');
        fwprint(stdout, files->d_name, 1);
      }
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        fwprint(stdout, tmpfile->d_name, 1);
        tmpfile = tmpfile->duplicates;
      }
      if (files->next != NULL) printf("\n");

    }

    files = files->next;
  }
  return;
}


/* Count the following statistics:
   - Maximum number of files in a duplicate set (length of longest dupe chain)
   - Number of non-zero-length files that have duplicates (if n_files != NULL)
   - Total number of duplicate file sets (groups) */
static unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max,
                unsigned int * const restrict n_files) {
  unsigned int groups = 0;

  *max = 0;
  if (n_files) *n_files = 0;

  while (files) {
    unsigned int n_dupes;
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
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


#ifdef ENABLE_BTRFS
static char *dedupeerrstr(int err) {
  static char buf[256];

  buf[sizeof(buf)-1] = '\0';
  if (err == BTRFS_SAME_DATA_DIFFERS) {
    snprintf(buf, sizeof(buf), "BTRFS_SAME_DATA_DIFFERS (data modified in the meantime?)");
    return buf;
  } else if (err < 0) {
    return strerror(-err);
  } else {
    snprintf(buf, sizeof(buf), "Unknown error %d", err);
    return buf;
  }
}

void dedupefiles(file_t * restrict files)
{
  struct btrfs_ioctl_same_args *same;
  char **dupe_filenames; /* maps to same->info indices */

  file_t *curfile;
  unsigned int n_dupes, max_dupes, cur_info;
  unsigned int cur_file = 0, max_files, total_files = 0;

  int fd;
  int ret, status, readonly;

  LOUD(fprintf(stderr, "\nRunning dedupefiles()\n");)

  /* Find the largest dupe set, alloc space to hold structs for it */
  get_max_dupes(files, &max_dupes, &max_files);
  same = calloc(sizeof(struct btrfs_ioctl_same_args) +
                sizeof(struct btrfs_ioctl_same_extent_info) * max_dupes, 1);
  dupe_filenames = malloc(max_dupes * sizeof(char *));
  LOUD(fprintf(stderr, "dedupefiles structs: alloc1 size %lu => %p, alloc2 size %lu => %p\n",
        sizeof(struct btrfs_ioctl_same_args) + sizeof(struct btrfs_ioctl_same_extent_info) * max_dupes,
        (void *)same, max_dupes * sizeof(char *), (void *)dupe_filenames);)
  if (!same || !dupe_filenames) oom("dedupefiles() structures");

  /* Main dedupe loop */
  while (files) {
    if (ISFLAG(files->flags, F_HAS_DUPES) && files->size) {
      cur_file++;
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        fprintf(stderr, "Dedupe [%u/%u] %u%% \r", cur_file, max_files,
            cur_file * 100 / max_files);
      }

      /* Open each file to be deduplicated */
      cur_info = 0;
      for (curfile = files->duplicates; curfile; curfile = curfile->duplicates) {
        int errno2;

        /* Never allow hard links to be passed to dedupe */
        if (curfile->device == files->device && curfile->inode == files->inode) {
          LOUD(fprintf(stderr, "skipping hard linked file pair: '%s' = '%s'\n", curfile->d_name, files->d_name);)
          continue;
        }

        dupe_filenames[cur_info] = curfile->d_name;
        readonly = 0;
        if (access(curfile->d_name, W_OK) != 0) readonly = 1;
        fd = open(curfile->d_name, O_RDWR);
        LOUD(fprintf(stderr, "opening loop: open('%s', O_RDWR) [%d]\n", curfile->d_name, fd);)

        /* If read-write open fails, privileged users can dedupe in read-only mode */
        if (fd == -1) {
          /* Preserve errno in case read-only fallback fails */
          LOUD(fprintf(stderr, "opening loop: open('%s', O_RDWR) failed: %s\n", curfile->d_name, strerror(errno));)
          errno2 = errno;
          fd = open(curfile->d_name, O_RDONLY);
          if (fd == -1) {
            LOUD(fprintf(stderr, "opening loop: fallback open('%s', O_RDONLY) failed: %s\n", curfile->d_name, strerror(errno));)
            fprintf(stderr, "Unable to open '%s': %s%s\n", curfile->d_name,
                strerror(errno2), readonly_msg[readonly]);
            continue;
          }
          LOUD(fprintf(stderr, "opening loop: fallback open('%s', O_RDONLY) succeeded\n", curfile->d_name);)
        }

        same->info[cur_info].fd = fd;
        same->info[cur_info].logical_offset = 0;
        cur_info++;
        total_files++;
      }
      n_dupes = cur_info;

      same->logical_offset = 0;
      same->length = files->size;
      same->dest_count = n_dupes;

      fd = open(files->d_name, O_RDONLY);
      LOUD(fprintf(stderr, "source: open('%s', O_RDONLY) [%d]\n", files->d_name, fd);)
      if (fd == -1) {
        fprintf(stderr, "unable to open(\"%s\", O_RDONLY): %s\n", files->d_name, strerror(errno));
        goto cleanup;
      }

      /* Call dedupe ioctl to pass the files to the kernel */
      ret = ioctl(fd, BTRFS_IOC_FILE_EXTENT_SAME, same);
      LOUD(fprintf(stderr, "dedupe: ioctl('%s' [%d], BTRFS_IOC_FILE_EXTENT_SAME, same) => %d\n", files->d_name, fd, ret);)
      if (close(fd) == -1) fprintf(stderr, "Unable to close(\"%s\"): %s\n", files->d_name, strerror(errno));

      if (ret < 0) {
        fprintf(stderr, "dedupe failed against file '%s' (%d matches): %s\n", files->d_name, n_dupes, strerror(errno));
        goto cleanup;
      }

      for (cur_info = 0; cur_info < n_dupes; cur_info++) {
        status = same->info[cur_info].status;
        if (status != 0) {
          if (same->info[cur_info].bytes_deduped == 0) {
            fprintf(stderr, "warning: dedupe failed: %s => %s: %s [%d]%s\n",
              files->d_name, dupe_filenames[cur_info], dedupeerrstr(status),
              status, readonly_msg[readonly]);
          } else {
            fprintf(stderr, "warning: dedupe only did %jd bytes: %s => %s: %s [%d]%s\n",
              (intmax_t)same->info[cur_info].bytes_deduped, files->d_name,
              dupe_filenames[cur_info], dedupeerrstr(status), status, readonly_msg[readonly]);
          }
        }
      }

cleanup:
      for (cur_info = 0; cur_info < n_dupes; cur_info++) {
        if (close(same->info[cur_info].fd) == -1) {
          fprintf(stderr, "unable to close(\"%s\"): %s", dupe_filenames[cur_info],
            strerror(errno));
        }
      }

    } /* has dupes */

    files = files->next;
  }

  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "Deduplication done (%d files processed)\n", total_files);
  free(same);
  free(dupe_filenames);
  return;
}
#endif /* ENABLE_BTRFS */

static void deletefiles(file_t *files, int prompt, FILE *tty)
{
  unsigned int counter, groups;
  unsigned int curgroup = 0;
  file_t *tmpfile;
  file_t **dupelist;
  unsigned int *preserve;
  char *preservestr;
  char *token;
  char *tstr;
  unsigned int number, sum, max, x;
  size_t i;

  groups = get_max_dupes(files, &max, NULL);

  max++;

  dupelist = (file_t **) malloc(sizeof(file_t*) * max);
  preserve = (unsigned int *) malloc(sizeof(int) * max);
  preservestr = (char *) malloc(INPUT_SIZE);

  if (!dupelist || !preserve || !preservestr) oom("deletefiles() structures");

  for (; files; files = files->next) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      curgroup++;
      counter = 1;
      dupelist[counter] = files;

      if (prompt) {
        printf("[%u] ", counter); fwprint(stdout, files->d_name, 1);
      }

      tmpfile = files->duplicates;

      while (tmpfile) {
        dupelist[++counter] = tmpfile;
        if (prompt) {
          printf("[%u] ", counter); fwprint(stdout, tmpfile->d_name, 1);
        }
        tmpfile = tmpfile->duplicates;
      }

      if (prompt) printf("\n");

      /* preserve only the first file */
      if (!prompt) {
        preserve[1] = 1;
        for (x = 2; x <= counter; x++) preserve[x] = 0;
      } else do {
        /* prompt for files to preserve */
        printf("Set %u of %u: keep which files? (1 - %u, [a]ll, [n]one)",
          curgroup, groups, counter);
        if (ISFLAG(flags, F_SHOWSIZE)) printf(" (%ju byte%c each)", (uintmax_t)files->size,
          (files->size != 1) ? 's' : ' ');
        printf(": ");
        fflush(stdout);

        /* treat fgets() failure as if nothing was entered */
        if (!fgets(preservestr, INPUT_SIZE, tty)) preservestr[0] = '\n';

        i = strlen(preservestr) - 1;

        /* tail of buffer must be a newline */
        while (preservestr[i] != '\n') {
          tstr = (char *)realloc(preservestr, strlen(preservestr) + 1 + INPUT_SIZE);
          if (!tstr) oom("deletefiles() prompt string");

          preservestr = tstr;
          if (!fgets(preservestr + i + 1, INPUT_SIZE, tty))
          {
            preservestr[0] = '\n'; /* treat fgets() failure as if nothing was entered */
            break;
          }
          i = strlen(preservestr) - 1;
        }

        for (x = 1; x <= counter; x++) preserve[x] = 0;

        token = strtok(preservestr, " ,\n");
        if (token != NULL && (*token == 'n' || *token == 'N')) goto preserve_none;

        while (token != NULL) {
          if (*token == 'a' || *token == 'A')
            for (x = 0; x <= counter; x++) preserve[x] = 1;

          number = 0;
          sscanf(token, "%u", &number);
          if (number > 0 && number <= counter) preserve[number] = 1;

          token = strtok(NULL, " ,\n");
        }

        for (sum = 0, x = 1; x <= counter; x++) sum += preserve[x];
      } while (sum < 1); /* save at least one file */
preserve_none:

      printf("\n");

      for (x = 1; x <= counter; x++) {
        if (preserve[x]) {
          printf("   [+] "); fwprint(stdout, dupelist[x]->d_name, 1);
        } else {
#ifdef UNICODE
          if (!M2W(dupelist[x]->d_name, wstr)) {
            printf("   [!] "); fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- MultiByteToWideChar failed\n");
            continue;
          }
#endif
          if (file_has_changed(dupelist[x])) {
            printf("   [!] "); fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- file changed since being scanned\n");
#ifdef UNICODE
          } else if (DeleteFile(wstr) != 0) {
#else
          } else if (remove(dupelist[x]->d_name) == 0) {
#endif
            printf("   [-] "); fwprint(stdout, dupelist[x]->d_name, 1);
          } else {
            printf("   [!] "); fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- unable to delete file\n");
          }
        }
      }
      printf("\n");
    }
  }
  free(dupelist);
  free(preserve);
  free(preservestr);
  return;
}


static int sort_pairs_by_param_order(file_t *f1, file_t *f2)
{
  if (!ISFLAG(flags, F_USEPARAMORDER)) return 0;
  if (f1->user_order < f2->user_order) return -sort_direction;
  if (f1->user_order > f2->user_order) return sort_direction;
  return 0;
}


static int sort_pairs_by_mtime(file_t *f1, file_t *f2)
{
  int po = sort_pairs_by_param_order(f1, f2);

  if (po != 0) return po;

  if (f1->mtime < f2->mtime) return -sort_direction;
  else if (f1->mtime > f2->mtime) return sort_direction;

  return 0;
}


static int sort_pairs_by_filename(file_t *f1, file_t *f2)
{
  int po = sort_pairs_by_param_order(f1, f2);

  if (po != 0) return po;

  return numeric_sort(f1->d_name, f2->d_name, sort_direction);
}


static void registerpair(file_t **matchlist, file_t *newmatch,
                int (*comparef)(file_t *f1, file_t *f2))
{
  file_t *traverse;
  file_t *back;

  /* NULL pointer sanity checks */
  if (!matchlist || !newmatch || !comparef) return;

  LOUD(fprintf(stderr, "registerpair: '%s', '%s'\n", (*matchlist)->d_name, newmatch->d_name);)
  SETFLAG((*matchlist)->flags, F_HAS_DUPES);
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
        SETFLAG(newmatch->flags, F_HAS_DUPES);
        CLEARFLAG(traverse->flags, F_HAS_DUPES); /* flag is only for first file in dupe chain */
      } else back->duplicates = newmatch;

      break;
    } else {
      if (traverse->duplicates == 0) {
        traverse->duplicates = newmatch;
        if (!back) SETFLAG(traverse->flags, F_HAS_DUPES);

        break;
      }
    }

    back = traverse;
    traverse = traverse->duplicates;
  }
  return;
}


#ifndef NO_HARDLINKS
static inline void linkfiles(file_t *files, int hard)
{
  static file_t *tmpfile;
  static file_t *srcfile;
  static file_t *curfile;
  static file_t ** restrict dupelist;
  static unsigned int counter;
  static unsigned int max = 0;
  static unsigned int x = 0;
  static size_t name_len = 0;
  static int i, success;
#ifndef NO_SYMLINKS
  static unsigned int symsrc;
  static char rel_path[PATHBUF_SIZE];
#endif
  static char temp_path[PATHBUF_SIZE];

  LOUD(fprintf(stderr, "Running linkfiles(%d)\n", hard);)
  curfile = files;

  while (curfile) {
    if (ISFLAG(curfile->flags, F_HAS_DUPES)) {
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

  if (!dupelist) oom("linkfiles() dupelist");

  while (files) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      counter = 1;
      dupelist[counter] = files;

      tmpfile = files->duplicates;

      while (tmpfile) {
       counter++;
       dupelist[counter] = tmpfile;
       tmpfile = tmpfile->duplicates;
      }

      /* Link every file to the first file */

      if (hard) {
        x = 2;
        srcfile = dupelist[1];
      } else {
#ifndef NO_SYMLINKS
        x = 1;
        /* Symlinks should target a normal file if one exists */
        srcfile = NULL;
        for (symsrc = 1; symsrc <= counter; symsrc++) {
          if (!ISFLAG(dupelist[symsrc]->flags, F_IS_SYMLINK)) {
            srcfile = dupelist[symsrc];
            break;
          }
        }
        /* If no normal file exists, abort */
        if (srcfile == NULL) continue;
#else
        fprintf(stderr, "internal error: linkfiles(soft) called without symlink support\nPlease report this to the author as a program bug\n");
        exit(EXIT_FAILURE);
#endif
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        printf("[SRC] "); fwprint(stdout, srcfile->d_name, 1);
      }
      for (; x <= counter; x++) {
        if (hard == 1) {
          /* Can't hard link files on different devices */
          if (srcfile->device != dupelist[x]->device) {
            fprintf(stderr, "warning: hard link target on different device, not linking:\n-//-> ");
            fwprint(stderr, dupelist[x]->d_name, 1);
            continue;
          } else {
            /* The devices for the files are the same, but we still need to skip
             * anything that is already hard linked (-L and -H both set) */
            if (srcfile->inode == dupelist[x]->inode) {
              /* Don't show == arrows when not matching against other hard links */
              if (ISFLAG(flags, F_CONSIDERHARDLINKS))
                if (!ISFLAG(flags, F_HIDEPROGRESS)) {
                  printf("-==-> "); fwprint(stdout, dupelist[x]->d_name, 1);
                }
            continue;
            }
          }
        } else {
          /* Symlink prerequisite check code can go here */
          /* Do not attempt to symlink a file to itself or to another symlink */
#ifndef NO_SYMLINKS
          if (ISFLAG(dupelist[x]->flags, F_IS_SYMLINK) && 
              ISFLAG(dupelist[symsrc]->flags, F_IS_SYMLINK)) continue;
          if (x == symsrc) continue;
#endif
        }
#ifdef UNICODE
        if (!M2W(dupelist[x]->d_name, wname)) {
          fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
#endif /* UNICODE */

        /* Do not attempt to hard link files for which we don't have write access */
#ifdef ON_WINDOWS
        if (dupelist[x]->mode & FILE_ATTRIBUTE_READONLY)
#else
        if (access(dupelist[x]->d_name, W_OK) != 0)
#endif
        {
          fprintf(stderr, "warning: link target is a read-only file, not linking:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
        /* Check file pairs for modification before linking */
        /* Safe linking: don't actually delete until the link succeeds */
        i = file_has_changed(srcfile);
        if (i) {
          fprintf(stderr, "warning: source file modified since scanned; changing source file:\n[SRC] ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          LOUD(fprintf(stderr, "file_has_changed: %d\n", i);)
          srcfile = dupelist[x];
          continue;
        }
        if (file_has_changed(dupelist[x])) {
          fprintf(stderr, "warning: target file modified since scanned, not linking:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
#ifdef ON_WINDOWS
        /* For Windows, the hard link count maximum is 1023 (+1); work around
         * by skipping linking or changing the link source file as needed */
        if (win_stat(srcfile->d_name, &ws) != 0) {
          fprintf(stderr, "warning: win_stat() on source file failed, changing source file:\n[SRC] ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          srcfile = dupelist[x];
          continue;
        }
        if (ws.nlink >= 1024) {
          fprintf(stderr, "warning: maximum source link count reached, changing source file:\n[SRC] ");
          srcfile = dupelist[x];
          continue;
        }
        if (win_stat(dupelist[x]->d_name, &ws) != 0) continue;
        if (ws.nlink >= 1024) {
          fprintf(stderr, "warning: maximum destination link count reached, skipping:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
#endif

        /* Make sure the name will fit in the buffer before trying */
        name_len = strlen(dupelist[x]->d_name) + 14;
        if (name_len > PATHBUF_SIZE) continue;
        /* Assemble a temporary file name */
        strcpy(temp_path, dupelist[x]->d_name);
        strcat(temp_path, ".__jdupes__.tmp");
        /* Rename the source file to the temporary name */
#ifdef UNICODE
        if (!M2W(temp_path, wname2)) {
          fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, srcfile->d_name, 1);
          continue;
        }
        i = MoveFile(wname, wname2) ? 0 : 1;
#else
        i = rename(dupelist[x]->d_name, temp_path);
#endif
        if (i != 0) {
          fprintf(stderr, "warning: cannot move link target to a temporary name, not linking:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          /* Just in case the rename succeeded yet still returned an error, roll back the rename */
#ifdef UNICODE
          MoveFile(wname2, wname);
#else
          rename(temp_path, dupelist[x]->d_name);
#endif
          continue;
        }

        /* Create the desired hard link with the original file's name */
        errno = 0;
#ifdef ON_WINDOWS
 #ifdef UNICODE
        if (!M2W(srcfile->d_name, wname2)) {
          fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, srcfile->d_name, 1);
          continue;
        }
        if (CreateHardLinkW((LPCWSTR)wname, (LPCWSTR)wname2, NULL) == TRUE) success = 1;
 #else
        if (CreateHardLink(dupelist[x]->d_name, srcfile->d_name, NULL) == TRUE) success = 1;
 #endif
#else
        success = 0;
        if (hard) {
          if (link(srcfile->d_name, dupelist[x]->d_name) == 0) success = 1;
 #ifdef NO_SYMLINKS
        }
 #else
        } else {
          i = make_relative_link_name(srcfile->d_name, dupelist[x]->d_name, rel_path);
          LOUD(fprintf(stderr, "symlink GRN: %s to %s = %s\n", srcfile->d_name, dupelist[x]->d_name, rel_path));
          if (i < 0) {
            fprintf(stderr, "warning: make_relative_link_name() failed (%d)\n", i);
          } else if (i == 1) {
            fprintf(stderr, "warning: files to be linked have the same canonical path; not linking\n");
          } else if (symlink(rel_path, dupelist[x]->d_name) == 0) success = 1;
        }
 #endif /* NO_SYMLINKS */
#endif /* ON_WINDOWS */
        if (success) {
          if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("%s %s\n", (hard ? "---->" : "-@@->"), dupelist[x]->d_name);
        } else {
          /* The link failed. Warn the user and put the link target back */
          if (!ISFLAG(flags, F_HIDEPROGRESS)) {
            printf("-//-> "); fwprint(stderr, dupelist[x]->d_name, 1);
          }
          fprintf(stderr, "warning: unable to link '"); fwprint(stderr, dupelist[x]->d_name, 0);
          fprintf(stderr, "' -> '"); fwprint(stderr, srcfile->d_name, 0);
          fprintf(stderr, "': %s\n", strerror(errno));
#ifdef UNICODE
          if (!M2W(temp_path, wname2)) {
            fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, temp_path, 1);
            continue;
          }
          i = MoveFile(wname2, wname) ? 0 : 1;
#else
          i = rename(temp_path, dupelist[x]->d_name);
#endif
          if (i != 0) {
            fprintf(stderr, "error: cannot rename temp file back to original\n");
            fprintf(stderr, "original: "); fwprint(stderr, dupelist[x]->d_name, 1);
            fprintf(stderr, "current:  "); fwprint(stderr, temp_path, 1);
          }
          continue;
        }

        /* Remove temporary file to clean up; if we can't, reverse the linking */
#ifdef UNICODE
          if (!M2W(temp_path, wname2)) {
            fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, temp_path, 1);
            continue;
          }
        i = DeleteFile(wname2) ? 0 : 1;
#else
        i = remove(temp_path);
#endif
        if (i != 0) {
          /* If the temp file can't be deleted, there may be a permissions problem
           * so reverse the process and warn the user */
          fprintf(stderr, "\nwarning: can't delete temp file, reverting: ");
          fwprint(stderr, temp_path, 1);
#ifdef UNICODE
          i = DeleteFile(wname) ? 0 : 1;
#else
          i = remove(dupelist[x]->d_name);
#endif
          /* This last error really should not happen, but we can't assume it won't */
          if (i != 0) fprintf(stderr, "\nwarning: couldn't remove link to restore original file\n");
          else {
#ifdef UNICODE
            i = MoveFile(wname2, wname) ? 0 : 1;
#else
            i = rename(temp_path, dupelist[x]->d_name);
#endif
            if (i != 0) {
              fprintf(stderr, "\nwarning: couldn't revert the file to its original name\n");
              fprintf(stderr, "original: "); fwprint(stderr, dupelist[x]->d_name, 1);
              fprintf(stderr, "current:  "); fwprint(stderr, temp_path, 1);
            }
          }
        }
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("\n");
    }
    files = files->next;
  }

  free(dupelist);
  return;
}
#endif /* NO_HARDLINKS */


static inline void help_text(void)
{
  printf("Usage: jdupes [options] DIRECTORY...\n\n");

  printf(" -A --nohidden    \texclude hidden files from consideration\n");
#ifdef ENABLE_BTRFS
  printf(" -B --dedupe      \tSend matches to btrfs for block-level deduplication\n");
#endif
  printf(" -d --delete      \tprompt user for files to preserve and delete all\n");
  printf("                  \tothers; important: under particular circumstances,\n");
  printf("                  \tdata may be lost when using this option together\n");
  printf("                  \twith -s or --symlinks, or when specifying a\n");
  printf("                  \tparticular directory more than once; refer to the\n");
  printf("                  \tdocumentation for additional information\n");
  printf(" -f --omitfirst   \tomit the first file in each set of matches\n");
  printf(" -h --help        \tdisplay this help message\n");
#ifndef NO_HARDLINKS
  printf(" -H --hardlinks   \ttreat any linked files as duplicate files. Normally\n");
  printf("                  \tlinked files are treated as non-duplicates for safety\n");
#endif
  printf(" -i --reverse     \treverse (invert) the match sort order\n");
  printf(" -I --isolate     \tfiles in the same specified directory won't match\n");
#ifndef NO_SYMLINKS
  printf(" -l --linksoft    \tmake relative symlinks for duplicates w/o prompting\n");
#endif
#ifndef NO_HARDLINKS
  printf(" -L --linkhard    \thard link all duplicate files without prompting\n");
 #ifdef ON_WINDOWS
  printf("                  \tWindows allows a maximum of 1023 hard links per file\n");
 #endif /* ON_WINDOWS */
#endif /* NO_HARDLINKS */
  printf(" -m --summarize   \tsummarize dupe information\n");
  printf(" -n --noempty     \texclude zero-length files from consideration\n");
  printf(" -N --noprompt    \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
  printf(" -O --paramorder  \tParameter order is more important than selected -O sort\n");
  printf("                  \tmtime (BY=time) or filename (BY=name, the default)\n");
#ifndef NO_PERMS
  printf(" -p --permissions \tdon't consider files with different owner/group or\n");
  printf("                  \tpermission bits as duplicates\n");
#endif
  printf(" -r --recurse     \tfor every directory given follow subdirectories\n");
  printf("                  \tencountered within\n");
  printf(" -R --recurse:    \tfor each directory given after this option follow\n");
  printf("                  \tsubdirectories encountered within (note the ':' at\n");
  printf("                  \tthe end of the option, manpage for more details)\n");
#ifndef NO_SYMLINKS
  printf(" -s --symlinks    \tfollow symlinks\n");
#endif
  printf(" -S --size        \tshow size of duplicate files\n");
  printf(" -q --quiet       \thide progress indicator\n");
/* This is undocumented in the quick help because it is a dangerous option. If you
 * really want it, uncomment it here, and may your data rest in peace. */
/*  printf(" -Q --quick       \tskip byte-by-byte duplicate verification. WARNING:\n");
  printf("                  \tthis may delete non-duplicates! Read the manual first!\n"); */
  printf(" -v --version     \tdisplay jdupes version and license information\n");
  printf(" -x --xsize=SIZE  \texclude files of size < SIZE bytes from consideration\n");
  printf("    --xsize=+SIZE \t'+' specified before SIZE, exclude size > SIZE\n");
  printf("                  \tK/M/G size suffixes can be used (case-insensitive)\n");
  printf(" -Z --softabort   \tIf the user aborts (i.e. CTRL-C) act on matches so far\n");
#ifdef OMIT_GETOPT_LONG
  printf("Note: Long options are not supported in this build.\n\n");
#endif
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
  static char *endptr;
#ifndef USE_TREE_REBALANCE
  filetree_t *checktree = NULL;
#endif
  static int firstrecurse;
  static int opt;
  static int pm = 1;
  static ordertype_t ordertype = ORDER_NAME;

#ifndef OMIT_GETOPT_LONG
  static const struct option long_options[] =
  {
    { "loud", 0, 0, '@' },
    { "nohidden", 0, 0, 'A' },
    { "dedupe", 0, 0, 'B' },
    { "delete", 0, 0, 'd' },
    { "debug", 0, 0, 'D' },
    { "omitfirst", 0, 0, 'f' },
    { "help", 0, 0, 'h' },
#ifndef NO_HARDLINKS
    { "hardlinks", 0, 0, 'H' },
    { "linkhard", 0, 0, 'L' },
#endif
    { "reverse", 0, 0, 'i' },
    { "isolate", 0, 0, 'I' },
    { "summarize", 0, 0, 'm'},
    { "summary", 0, 0, 'm' },
    { "noempty", 0, 0, 'n' },
    { "noprompt", 0, 0, 'N' },
    { "order", 1, 0, 'o' },
    { "paramorder", 0, 0, 'O' },
#ifndef NO_PERMS
    { "permissions", 0, 0, 'p' },
#endif
    { "quiet", 0, 0, 'q' },
    { "quick", 0, 0, 'Q' },
    { "recurse", 0, 0, 'r' },
    { "recursive", 0, 0, 'r' },
    { "recurse:", 0, 0, 'R' },
    { "recursive:", 0, 0, 'R' },
#ifndef NO_SYMLINKS
    { "linksoft", 0, 0, 'l' },
    { "symlinks", 0, 0, 's' },
#endif
    { "size", 0, 0, 'S' },
    { "version", 0, 0, 'v' },
    { "xsize", 1, 0, 'x' },
    { "softabort", 0, 0, 'Z' },
    { 0, 0, 0, 0 }
  };
#define GETOPT getopt_long
#else
#define GETOPT getopt
#endif

#if PATHBUF_SIZE < PATH_MAX
  fprintf(stderr, "\n----------\n");
  fprintf(stderr, "WARNING: This program's PATHBUF_SIZE is too small and it may destroy data!\n");
  fprintf(stderr, "Don't trust it with anything important. THIS IS YOUR ONLY WARNING.\n");
  fprintf(stderr, "Current settings:  PATHBUF_SIZE = %d   PATH_MAX = %d\n", PATHBUF_SIZE, PATH_MAX);
  fprintf(stderr, "----------\n");
#endif

#ifdef UNICODE
  /* Create a UTF-8 **argv from the wide version */
  static char **argv;
  argv = (char **)string_malloc(sizeof(char *) * argc);
  if (!argv) oom("main() unicode argv");
  widearg_to_argv(argc, wargv, argv);
  /* Only use UTF-16 for terminal output, else use UTF-8 */
  if (!_isatty(_fileno(stdout))) out_mode = _O_U8TEXT;
  else out_mode = _O_U16TEXT;
#endif /* UNICODE */

  program_name = argv[0];

  oldargv = cloneargs(argc, argv);

  while ((opt = GETOPT(argc, argv,
  "@ABdDfhHiIlLmnNOpqQrRsSvZo:x:"
#ifndef OMIT_GETOPT_LONG
          , long_options, NULL
#endif
         )) != EOF) {
    switch (opt) {
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
    case 'f':
      SETFLAG(flags, F_OMITFIRST);
      break;
    case 'h':
      help_text();
      exit(EXIT_FAILURE);
#ifndef NO_HARDLINKS
    case 'H':
      SETFLAG(flags, F_CONSIDERHARDLINKS);
      break;
    case 'L':
      SETFLAG(flags, F_HARDLINKFILES);
      break;
#endif
    case 'i':
      SETFLAG(flags, F_REVERSESORT);
      break;
    case 'I':
      SETFLAG(flags, F_ISOLATE);
      break;
    case 'm':
      SETFLAG(flags, F_SUMMARIZEMATCHES);
      break;
    case 'n':
      SETFLAG(flags, F_EXCLUDEEMPTY);
      break;
    case 'N':
      SETFLAG(flags, F_NOPROMPT);
      break;
    case 'O':
      SETFLAG(flags, F_USEPARAMORDER);
      break;
    case 'p':
      SETFLAG(flags, F_PERMISSIONS);
      break;
    case 'q':
      SETFLAG(flags, F_HIDEPROGRESS);
      break;
    case 'Q':
      SETFLAG(flags, F_QUICKCOMPARE);
      break;
    case 'r':
      SETFLAG(flags, F_RECURSE);
      break;
    case 'R':
      SETFLAG(flags, F_RECURSEAFTER);
      break;
#ifndef NO_SYMLINKS
    case 'l':
      SETFLAG(flags, F_MAKESYMLINKS);
      break;
    case 's':
      SETFLAG(flags, F_FOLLOWLINKS);
      break;
#endif
    case 'S':
      SETFLAG(flags, F_SHOWSIZE);
      break;
    case 'Z':
      SETFLAG(flags, F_SOFTABORT);
      break;
    case 'x':
      SETFLAG(flags, F_EXCLUDESIZE);
      if (*optarg == '+') {
        excludetype = LARGERTHAN;
        optarg++;
      }
      excludesize = (uintmax_t)strtoull(optarg, &endptr, 0);
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
        fprintf(stderr, "invalid value for --xsize: '%s'\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case '@':
#ifdef LOUD_DEBUG
      SETFLAG(flags, F_DEBUG | F_LOUD | F_HIDEPROGRESS);
#endif
      break;
    case 'v':
      printf("jdupes %s (%s)\n", VER, VERDATE);
      printf("Compile-time extensions:");
      if (*extensions != NULL) {
        int c = 0;
        while (extensions[c] != NULL) {
          printf(" %s", extensions[c]);
          c++;
        }
      } else printf(" none");
      printf("\nCopyright (C) 2015-2016 by Jody Bruchon\n");
      printf("\nPermission is hereby granted, free of charge, to any person\n");
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
    case 'o':
      if (!strncasecmp("name", optarg, 5)) {
        ordertype = ORDER_NAME;
      } else if (!strncasecmp("time", optarg, 5)) {
        ordertype = ORDER_TIME;
      } else {
        fprintf(stderr, "invalid value for --order: '%s'\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 'B':
#ifdef ENABLE_BTRFS
    SETFLAG(flags, F_DEDUPEFILES);
    /* btrfs will do the byte-for-byte check itself */
    SETFLAG(flags, F_QUICKCOMPARE);
    /* It is completely useless to dedupe zero-length extents */
    SETFLAG(flags, F_EXCLUDEEMPTY);
#else
    fprintf(stderr, "This program was built without btrfs support\n");
    exit(EXIT_FAILURE);
#endif
    break;

    default:
      fprintf(stderr, "Try `jdupes --help' for more information.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "no directories specified (use -h option for help)\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_ISOLATE) && optind == (argc - 1)) {
    fprintf(stderr, "Isolation requires at least two directories on the command line\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_RECURSE) && ISFLAG(flags, F_RECURSEAFTER)) {
    fprintf(stderr, "options --recurse and --recurse: are not compatible\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_SUMMARIZEMATCHES) && ISFLAG(flags, F_DELETEFILES)) {
    fprintf(stderr, "options --summarize and --delete are not compatible\n");
    exit(EXIT_FAILURE);
  }

#ifdef ENABLE_BTRFS
  if (ISFLAG(flags, F_CONSIDERHARDLINKS) && ISFLAG(flags, F_DEDUPEFILES))
    fprintf(stderr, "warning: option --dedupe overrides the behavior of --hardlinks\n");
#endif

  /* If pm == 0, call printmatches() */
  pm = !!ISFLAG(flags, F_SUMMARIZEMATCHES) +
      !!ISFLAG(flags, F_DELETEFILES) +
      !!ISFLAG(flags, F_HARDLINKFILES) +
      !!ISFLAG(flags, F_MAKESYMLINKS) +
      !!ISFLAG(flags, F_DEDUPEFILES);

  if (pm > 1) {
      fprintf(stderr, "Only one of --summarize, --delete, --linkhard, --linksoft, or --dedupe\nmay be used\n");
      exit(EXIT_FAILURE);
  }
  if (pm == 0) SETFLAG(flags, F_PRINTMATCHES);

  if (ISFLAG(flags, F_RECURSEAFTER)) {
    firstrecurse = nonoptafter("--recurse:", argc, oldargv, argv, optind);

    if (firstrecurse == argc)
      firstrecurse = nonoptafter("-R", argc, oldargv, argv, optind);

    if (firstrecurse == argc) {
      fprintf(stderr, "-R option must be isolated from other options\n");
      exit(EXIT_FAILURE);
    }

    /* F_RECURSE is not set for directories before --recurse: */
    for (int x = optind; x < firstrecurse; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, 0);
      user_dir_count++;
    }

    /* Set F_RECURSE for directories after --recurse: */
    SETFLAG(flags, F_RECURSE);

    for (int x = firstrecurse; x < argc; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, 1);
      user_dir_count++;
    }
  } else {
    for (int x = optind; x < argc; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, ISFLAG(flags, F_RECURSE));
      user_dir_count++;
    }
  }

  if (ISFLAG(flags, F_REVERSESORT)) sort_direction = -1;
  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\n");
  if (!files) exit(EXIT_SUCCESS);

  curfile = files;

  /* Catch CTRL-C */
  signal(SIGINT, sighandler);

  while (curfile) {
    static uintmax_t progress = 0;
    static uintmax_t dupecount = 0;
    static file_t **match = NULL;
    static FILE *file1;
    static FILE *file2;
    static off_t delay = DELAY_COUNT;
#ifdef USE_TREE_REBALANCE
    static unsigned int depth_threshold = INITIAL_DEPTH_THRESHOLD;
#endif

    if (interrupt) {
      fprintf(stderr, "\nStopping file scan due to user abort\n");
      if (!ISFLAG(flags, F_SOFTABORT)) exit(EXIT_FAILURE);
      interrupt = 0;  /* reset interrupt for re-use */
      goto skip_file_scan;
    }

    LOUD(fprintf(stderr, "\nMAIN: current file: %s\n", curfile->d_name));

    if (!checktree) registerfile(&checktree, NONE, curfile);
    else match = checkmatch(checktree, curfile);

#ifdef USE_TREE_REBALANCE
    /* Rebalance the match tree after a certain number of files processed */
    if (max_depth > depth_threshold) {
      rebalance_tree(checktree);
      max_depth = 0;
      if (depth_threshold < 512) depth_threshold <<= 1;
      else depth_threshold += 64;
    }
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
        LOUD(fprintf(stderr, "MAIN: notice: quick compare match (-Q)\n"));
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
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
        curfile = curfile->next;
        continue;
      }

      if (confirmmatch(file1, file2)) {
        LOUD(fprintf(stderr, "MAIN: registering matched file pair\n"));
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
      if (curfile != NULL && did_long_work) {
        delay += (curfile->size >> 20);
        did_long_work = 0;
      }
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

skip_file_scan:
  /* Stop catching CTRL+C */
  signal(SIGINT, SIG_DFL);
  if (ISFLAG(flags, F_DELETEFILES)) {
    if (ISFLAG(flags, F_NOPROMPT)) deletefiles(files, 0, 0);
    else deletefiles(files, 1, stdin);
  }
  if (ISFLAG(flags, F_SUMMARIZEMATCHES)) summarizematches(files);
#ifndef NO_SYMLINKS
  if (ISFLAG(flags, F_MAKESYMLINKS)) linkfiles(files, 0);
#endif
#ifndef NO_HARDLINKS
  if (ISFLAG(flags, F_HARDLINKFILES)) linkfiles(files, 1);
#endif /* NO_HARDLINKS */
#ifdef ENABLE_BTRFS
  if (ISFLAG(flags, F_DEDUPEFILES)) dedupefiles(files);
#endif /* ENABLE_BTRFS */
  if (ISFLAG(flags, F_PRINTMATCHES)) printmatches(files);

  string_malloc_destroy();

#ifdef DEBUG
  if (ISFLAG(flags, F_DEBUG)) {
    fprintf(stderr, "\n%d partial (+%d small) -> %d full hash -> %d full (%d partial elim) (%d hash%u fail)\n",
        partial_hash, small_file, full_hash, partial_to_full,
        partial_elim, hash_fail, (unsigned int)sizeof(hash_t)*8);
    fprintf(stderr, "%ju total files, %ju comparisons, branch L %u, R %u, both %u\n",
        filecount, comparisons, left_branch, right_branch,
        left_branch + right_branch);
    fprintf(stderr, "Max tree depth: %u; SMA: allocs %ju, free %ju, fail %ju, reuse %ju, scan %ju, tails %ju\n",
        max_depth, sma_allocs, sma_free_good, sma_free_ignored,
        sma_free_reclaimed, sma_free_scanned, sma_free_tails);
#ifdef ON_WINDOWS
 #ifndef NO_HARDLINKS
    if (ISFLAG(flags, F_HARDLINKFILES))
      fprintf(stderr, "Exclusions based on Windows hard link limit: %u\n", hll_exclude);
 #endif
#endif
  }
#endif /* DEBUG */

  exit(EXIT_SUCCESS);
}
