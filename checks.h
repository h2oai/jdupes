/* jdupes file check functions
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_CHECKS_H
#define JDUPES_CHECKS_H

#ifdef __cplusplus
extern "C" {
#endif

int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2);
int check_singlefile(file_t * const restrict newfile);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_CHECKS_H */
