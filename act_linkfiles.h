/* jdupes action for hard and soft file linking
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef ACT_LINKFILES_H
#define ACT_LINKFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"
/* Don't define the function if it's just empty anyway */
#if !(defined NO_HARDLINKS && defined NO_SYMLINKS)
extern void linkfiles(file_t *files, const int linktype, const int only_current);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ACT_LINKFILES_H */
