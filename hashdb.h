/* File hash database management
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_HASHDB_H
#define JDUPES_HASHDB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "jdupes.h"

typedef struct _hashdb {
  struct _hashdb *left;
  struct _hashdb *right;
  uint64_t path_hash;
  uint64_t partialhash;
  uint64_t fullhash;
  char *path;
  jdupes_ino_t inode;
  time_t mtime;
  int hashcount;
} hashdb_t;

extern hashdb_t *hashdb;

extern hashdb_t *add_hashdb_entry(uint64_t path_hash, int pathlen, file_t *check);
extern void dump_hashdb(hashdb_t *cur);
extern int get_path_hash(char *path, uint64_t *path_hash);
extern int load_hash_database(char *dbname);
extern int save_hash_database(const char * const restrict dbname);
extern void read_hashdb_entry(file_t *file);
extern int write_hashdb_entry(FILE *db, hashdb_t *cur);
extern void hd16(char *a);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_HASHDB_H */
