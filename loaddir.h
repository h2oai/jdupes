/* jdupes directory scanning code
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_LOADDIR_H
#define JDUPES_LOADDIR_H

#ifdef __cplusplus
extern "C" {
#endif

//file_t *grokfile(const char * const restrict name, file_t * restrict * const restrict filelistp);
void loaddir(const char * const restrict dir, file_t * restrict * const restrict filelistp, int recurse);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_LOADDIR_H */
