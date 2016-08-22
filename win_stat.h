/*
 * Windows-native routines for getting stat()-like information
 *
 * Copyright (C) 2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2/v3 (your choice)
 */

#ifndef WIN_STAT_H
#define WIN_STAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MAN
#endif
#include <windows.h>

struct winstat {
	uint64_t inode;
	uint64_t size;
	uint32_t device;
	uint32_t nlink;
	uint32_t mode;
};

/* stat()-like macros for Windows "mode" flags (file attributes) */
#define WS_ISARCHIVE(mode) ((mode & FILE_ATTRIBUTE_ARCHIVE) ? 1 : 0)
#define WS_ISRO(mode) ((mode & FILE_ATTRIBUTE_READONLY) ? 1 : 0)
#define WS_ISHIDDEN(mode) ((mode & FILE_ATTRIBUTE_HIDDEN) ? 1 : 0)
#define WS_ISSYSTEM(mode) ((mode & FILE_ATTRIBUTE_SYSTEM) ? 1 : 0)
#define WS_ISCRYPT(mode) ((mode & FILE_ATTRIBUTE_ENCRYPTED) ? 1 : 0)
#define WS_ISDIR(mode) ((mode & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0)
#define WS_ISCOMPR(mode) ((mode & FILE_ATTRIBUTE_COMPRESSED) ? 1 : 0)
#define WS_ISREPARSE(mode) ((mode & FILE_ATTRIBUTE_REPARSE) ? 1 : 0)
#define WS_ISSPARSE(mode) ((mode & FILE_ATTRIBUTE_SPARSE) ? 1 : 0)
#define WS_ISTEMP(mode) ((mode & FILE_ATTRIBUTE_TEMPORARY) ? 1 : 0)

extern int win_stat(const char * const restrict filename, struct winstat * const restrict buf);

#ifdef __cplusplus
}
#endif

#endif	/* WIN_STAT_H */
