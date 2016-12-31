/* jdupes action for BTRFS block-level deduplication
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef ACT_DEDUPEFILES_H
#define ACT_DEDUPEFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"
extern void dedupefiles(file_t * restrict files);

#ifdef __cplusplus
}
#endif

#endif /* ACT_DEDUPEFILES_H */
