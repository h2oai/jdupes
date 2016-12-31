/* jdupes action for deleting duplicate files
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef ACT_DELETEFILES_H
#define ACT_DELETEFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"
extern void deletefiles(file_t *files, int prompt, FILE *tty);

#ifdef __cplusplus
}
#endif

#endif /* ACT_DELETEFILES_H */
