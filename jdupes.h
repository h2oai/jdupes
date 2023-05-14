/* jdupes main program header
 * See jdupes.c for license information */

#ifndef JDUPES_H
#define JDUPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __MINGW32__
 #ifndef ON_WINDOWS
  #define ON_WINDOWS 1
 #endif
 #define NO_SYMLINKS 1
 #define NO_PERMS 1
 #define NO_SIGACTION 1
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <io.h>
#endif /* Win32 */

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <libjodycode.h>

#ifndef USE_JODY_HASH
#include "xxhash.h"
#endif /* USE_JODY_HASH */

/* Set hash type (change this if swapping in a different hash function) */
#ifndef USE_JODY_HASH
 typedef XXH64_hash_t jdupes_hash_t;
#else
 typedef jodyhash_t jdupes_hash_t;
#endif /* USE_JODY_HASH */

/* Some types are different on Windows */
#ifdef ON_WINDOWS
 typedef uint64_t jdupes_ino_t;
 typedef uint32_t jdupes_mode_t;
 extern const char dir_sep;
 #ifdef UNICODE
  extern const wchar_t *FILE_MODE_RO;
 #else
  extern const char *FILE_MODE_RO;
 #endif /* UNICODE */

#else /* Not Windows */
 #include <sys/stat.h>
 typedef ino_t jdupes_ino_t;
 typedef mode_t jdupes_mode_t;
 extern const char *FILE_MODE_RO;
 extern const char dir_sep;
 extern int usr1_toggle;
 #ifdef UNICODE
  #error Do not define UNICODE on non-Windows platforms.
  #undef UNICODE
 #endif
#endif /* _WIN32 || __MINGW32__ */

/* Windows + Unicode compilation */
#ifdef UNICODE
 #ifndef PATHBUF_SIZE
  #ifndef WPATH_MAX
   #define WPATH_MAX 8192
  #endif
  #define PATHBUF_SIZE WPATH_MAX
 #else
  #ifndef WPATH_MAX
   #define WPATH_MAX PATHBUF_SIZE
  #endif
 #endif /* PATHBUF_SIZE */
  typedef wchar_t wpath_t[WPATH_MAX];
  extern int out_mode;
  extern int err_mode;
 #define M2W(a,b) MultiByteToWideChar(CP_UTF8, 0, a, -1, (LPWSTR)b, WPATH_MAX)
 #define W2M(a,b) WideCharToMultiByte(CP_UTF8, 0, a, -1, (LPSTR)b, WPATH_MAX, NULL, NULL)
#endif /* UNICODE */

#define ISFLAG(a,b) ((a & b) == b)
#define SETFLAG(a,b) (a |= b)
#define CLEARFLAG(a,b) (a &= (~b))

/* Chunk sizing */
#ifndef CHUNK_SIZE
 #define CHUNK_SIZE 65536
#endif
#ifndef NO_CHUNKSIZE
 /* Larger chunk size makes large files process faster but uses more RAM */
 #define MIN_CHUNK_SIZE 4096
 #define MAX_CHUNK_SIZE 1048576 * 256
#endif /* NO_CHUNKSIZE */

/* Low memory option overrides */
#ifdef LOW_MEMORY
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
#else
 #define DBG(a)
#endif


/* Behavior modification flags */
extern uint64_t flags, a_flags;
#define F_RECURSE		(1ULL << 0)
#define F_HIDEPROGRESS		(1ULL << 1)
#define F_SOFTABORT		(1ULL << 2)
#define F_FOLLOWLINKS		(1ULL << 3)
#define F_INCLUDEEMPTY		(1ULL << 4)
#define F_CONSIDERHARDLINKS	(1ULL << 5)
#define F_RECURSEAFTER		(1ULL << 6)
#define F_NOPROMPT		(1ULL << 7)
#define F_EXCLUDEHIDDEN		(1ULL << 8)
#define F_PERMISSIONS		(1ULL << 9)
#define F_EXCLUDESIZE		(1ULL << 10)
#define F_QUICKCOMPARE		(1ULL << 11)
#define F_USEPARAMORDER		(1ULL << 12)
#define F_REVERSESORT		(1ULL << 13)
#define F_ISOLATE		(1ULL << 14)
#define F_ONEFS			(1ULL << 15)
#define F_PARTIALONLY		(1ULL << 16)
#define F_NOCHANGECHECK		(1ULL << 17)
#define F_NOTRAVCHECK		(1ULL << 18)
#define F_SKIPHASH		(1ULL << 19)
#define F_BENCHMARKSTOP		(1ULL << 29)

#define F_LOUD			(1ULL << 62)
#define F_DEBUG			(1ULL << 63)

/* Action-related flags */
#define FA_PRINTMATCHES		(1U << 0)
#define FA_PRINTUNIQUE		(1U << 1)
#define FA_OMITFIRST		(1U << 2)
#define FA_SUMMARIZEMATCHES	(1U << 3)
#define FA_DELETEFILES		(1U << 4)
#define FA_SHOWSIZE		(1U << 5)
#define FA_HARDLINKFILES	(1U << 6)
#define FA_DEDUPEFILES		(1U << 7)
#define FA_MAKESYMLINKS		(1U << 8)
#define FA_PRINTNULL		(1U << 9)
#define FA_PRINTJSON		(1U << 10)
#define FA_ERRORONDUPE		(1U << 11)

/* Per-file true/false flags */
#define FF_VALID_STAT		(1U << 0)
#define FF_HASH_PARTIAL		(1U << 1)
#define FF_HASH_FULL		(1U << 2)
#define FF_HAS_DUPES		(1U << 3)
#define FF_IS_SYMLINK		(1U << 4)
#define FF_NOT_UNIQUE		(1U << 5)

/* Extra print flags */
#define PF_PARTIAL		(1U << 0)
#define PF_EARLYMATCH		(1U << 1)
#define PF_FULLHASH		(1U << 2)

typedef enum {
  ORDER_NAME = 0,
  ORDER_TIME
} ordertype_t;

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

/* Per-file information */
typedef struct _file {
  struct _file *duplicates;
  struct _file *next;
  char *d_name;
  uint32_t flags;  /* Status flags */
  jdupes_mode_t mode;
  off_t size;
  dev_t device;
  jdupes_ino_t inode;
  jdupes_hash_t filehash_partial;
  jdupes_hash_t filehash;
#ifndef NO_MTIME
  time_t mtime;
#endif
#ifndef NO_ATIME
  time_t atime;
#endif
#ifndef NO_USER_ORDER
  unsigned int user_order; /* Order of the originating command-line parameter */
#endif
#ifndef NO_HARDLINKS
 #ifndef ON_WINDOWS
  nlink_t nlink;
 #else
  uint32_t nlink;  /* link count on Windows is always a DWORD */
 #endif
#endif
#ifndef NO_PERMS
  uid_t uid;
  gid_t gid;
#endif
} file_t;

typedef struct _filetree {
  file_t *file;
  struct _filetree *left;
  struct _filetree *right;
} filetree_t;

/* This gets used in many functions */
#ifdef ON_WINDOWS
 extern struct jc_winstat s;
 #define STAT jc_win_stat
#else
 extern struct stat s;
 #define STAT stat
#endif

/* Progress indicator variables */
extern uintmax_t filecount, progress, item_progress, dupecount;
extern struct timeval time1, time2;

extern char tempname[PATHBUF_SIZE * 2];

extern const char *feature_flags[];

extern int file_has_changed(file_t * const restrict file);
extern unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max,
		                unsigned int * const restrict n_files);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_H */
