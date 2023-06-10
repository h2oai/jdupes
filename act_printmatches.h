/* jdupes action for printing matched file sets to stdout
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef ACT_PRINTMATCHES_H
#define ACT_PRINTMATCHES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"
void printmatches(file_t * restrict files);
void printunique(file_t * restrict files);

#ifdef __cplusplus
}
#endif

#endif /* ACT_PRINTMATCHES_H */
