/* jdupes action for hard and soft file linking
 * This file is part of jdupes; see jdupes.c for license information */

#if !(defined NO_HARDLINKS && defined NO_SYMLINKS)

#ifndef ACT_LINKFILES_H
#define ACT_LINKFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"
extern void linkfiles(file_t *files, const int linktype, const int only_current);

#ifdef __cplusplus
}
#endif

#endif /* ACT_LINKFILES_H */

#endif /* NO_*LINKS */
