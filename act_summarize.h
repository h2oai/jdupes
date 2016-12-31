/* jdupes action for printing a summary of match stats to stdout
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef ACT_SUMMARIZE_H
#define ACT_SUMMARIZE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"
extern void summarizematches(const file_t * restrict files);

#ifdef __cplusplus
}
#endif

#endif /* ACT_SUMMARIZE_H */
