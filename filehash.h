/* jdupes file hashing function
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_FILEHASH_H
#define JDUPES_FILEHASH_H

#ifdef __cplusplus
extern "C" {
#endif

jdupes_hash_t *get_filehash(const file_t * const restrict checkfile, const size_t max_read);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_FILEHASH_H */
