/* jdupes file/dir stat()-related functions
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_FILESTAT_H
#define JDUPES_FILESTAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"

int file_has_changed(file_t * const restrict file);
int getfilestats(file_t * const restrict file);
/* Returns -1 if stat() fails, 0 if it's a directory, 1 if it's not */
int getdirstats(const char * const restrict name,
		jdupes_ino_t * const restrict inode, dev_t * const restrict dev,
		jdupes_mode_t * const restrict mode);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_FILESTAT_H */
