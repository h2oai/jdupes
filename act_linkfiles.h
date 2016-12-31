/* jdupes action for hard and soft file linking
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef ACT_LINKFILES_H
#define ACT_LINKFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"
extern void linkfiles(file_t *files, const int hard);

#ifdef __cplusplus
}
#endif

#endif /* ACT_LINKFILES_H */
