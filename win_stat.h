/* Windows-native routines for getting stat()-like information
 * See win_stat.c for license information */

#ifndef WIN_STAT_H
#define WIN_STAT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MAN
#endif
#include <windows.h>
#include <stdint.h>

struct winstat {
	uint64_t st_ino;
	int64_t st_size;
	uint32_t st_dev;
	uint32_t st_nlink;
	uint32_t st_mode;
	time_t st_ctime;
	time_t st_mtime;
	time_t st_atime;
};

/* stat() macros for Windows "mode" flags (file attributes) */
#define S_ISARCHIVE(st_mode) ((st_mode & FILE_ATTRIBUTE_ARCHIVE) ? 1 : 0)
#define S_ISRO(st_mode) ((st_mode & FILE_ATTRIBUTE_READONLY) ? 1 : 0)
#define S_ISHIDDEN(st_mode) ((st_mode & FILE_ATTRIBUTE_HIDDEN) ? 1 : 0)
#define S_ISSYSTEM(st_mode) ((st_mode & FILE_ATTRIBUTE_SYSTEM) ? 1 : 0)
#define S_ISCRYPT(st_mode) ((st_mode & FILE_ATTRIBUTE_ENCRYPTED) ? 1 : 0)
#define S_ISDIR(st_mode) ((st_mode & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0)
#define S_ISCOMPR(st_mode) ((st_mode & FILE_ATTRIBUTE_COMPRESSED) ? 1 : 0)
#define S_ISREPARSE(st_mode) ((st_mode & FILE_ATTRIBUTE_REPARSE) ? 1 : 0)
#define S_ISSPARSE(st_mode) ((st_mode & FILE_ATTRIBUTE_SPARSE) ? 1 : 0)
#define S_ISTEMP(st_mode) ((st_mode & FILE_ATTRIBUTE_TEMPORARY) ? 1 : 0)
#define S_ISREG(st_mode) ((st_mode & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1)

#ifndef WPATH_MAX
 #define WPATH_MAX 8192
#endif
#ifndef M2W
 #define M2W(a,b) MultiByteToWideChar(CP_UTF8, 0, a, -1, (LPWSTR)b, WPATH_MAX)
#endif

extern int win_stat(const char * const filename, struct winstat * const restrict buf);

#ifdef __cplusplus
}
#endif

#endif	/* WIN_STAT_H */
