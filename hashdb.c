/* hashdb */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "jdupes.h"
#include "libjodycode.h"


typedef struct _hashdb {
  struct _hashdb *left;
  struct _hashdb *right;
  uint64_t path_hash;
  char *path;
  time_t mtime;
  uint64_t partial_hash;
  uint64_t full_hash;
  unsigned int hashcount;
} hashdb_t;

static hashdb_t *hashdb = NULL;
static int hashdb_algo = 0;

#define HASHDB_MIN_VER 1
#define HASHDB_MAX_VER 1
#define SECS_TO_TIME(a,b) strftime(a, 32, "%F %T", localtime(b));


int read_hashdb(char *dbname);
int main(void) { return read_hashdb("foo"); }


hashdb_t *alloc_hashdb_entry(uint64_t hash, int pathlen)
{
  hashdb_t *file = (hashdb_t *)malloc(sizeof(hashdb_t) + pathlen + 1);
  hashdb_t *cur;

  if (file == NULL) return NULL;
  memset(file, 0, sizeof(hashdb_t));
  file->path = (char *)((uintptr_t)file + (uintptr_t)sizeof(hashdb_t));

  if (hashdb == NULL) {
    hashdb = file;
  } else {
    cur = hashdb;
    while (1) {
      if (cur->path_hash >= hash) {
        if (cur->left == NULL) {
          cur->left = file;
          break;
        } else {
          cur = cur->left;
          continue;
        }
      } else {
        if (cur->right == NULL) {
          cur->right = file;
          break;
        } else {
          cur = cur->right;
          continue;
        }
      }
    }
  }
  return file;
}

/* db header format: jdupes hashdb:dbversion,hashtype,update_mtime
 * db line format: hashcount,partial,full,mtime,hash */
int read_hashdb(char *dbname)
{
  FILE *db;
  char buf[PATH_MAX + 128];
  char date[32];
  char *field, *temp;
  int db_ver;
  time_t db_mtime;
  
  errno = 0;
  db = fopen(dbname, "rb");
  if (db == NULL) goto error_hashdb_open;

  /* Read header line */
  if ((fgets(buf, PATH_MAX + 127, db) == NULL) || (ferror(db) != 0)) goto error_hashdb_read;
//fprintf(stderr, "read hashdb: %s", buf);
  field = strtok(buf, ":");
  if (strcmp(field, "jdupes hashdb") != 0) goto error_hashdb_header;
  field = strtok(NULL, ":");
  temp = strtok(field, ",");
  db_ver = strtoul(temp, NULL, 10);
  temp = strtok(NULL, ",");
  hashdb_algo = strtoul(temp, NULL, 10);
  temp = strtok(NULL, ",");
  db_mtime = strtoull(temp, NULL, 16);
  SECS_TO_TIME(date, &db_mtime);
  fprintf(stderr, "ver %u, algo %u, mod %s\n", db_ver, hashdb_algo, date);
  if (db_ver < HASHDB_MIN_VER || db_ver > HASHDB_MAX_VER) goto error_hashdb_version;

  /* Read database entries */
  while (1) {
    unsigned int pathlen, linelen, linenum = 1, hashcount;
    uint64_t partial_hash, full_hash, path_hash;
    uint64_t aligned_path[(PATH_MAX + 128) / sizeof(uint64_t)];
    time_t mtime;
    char *path;
    hashdb_t *entry;

    errno = 0;
    if ((fgets(buf, PATH_MAX + 127, db) == NULL)) {
      if (ferror(db) != 0 || errno != 0) goto error_hashdb_read;
      break;
    }
//fprintf(stderr, "read hashdb: %s", buf);
    linenum++;
    linelen = strlen(buf);
    if (linelen < 54) goto error_hashdb_line;

    /* Split each entry into fields and
     * hashcount: 1 = partial only, 2 = partial and full */
    field = strtok(buf, ","); if (field == NULL) goto error_hashdb_line;
    hashcount = strtoul(field, NULL, 16);
    if (hashcount < 1 || hashcount > 2) goto error_hashdb_line;
    field = strtok(NULL, ","); if (field == NULL) goto error_hashdb_line;
    partial_hash = strtoull(field, NULL, 16);
    field = strtok(NULL, ","); if (field == NULL) goto error_hashdb_line;
    if (hashcount == 2) full_hash = strtoull(field, NULL, 16);
    field = strtok(NULL, ","); if (field == NULL) goto error_hashdb_line;
    mtime = (time_t)strtoull(field, NULL, 16);
    path = buf + 53;
    path = strtok(path, "\n"); if (path == NULL) goto error_hashdb_line;
    pathlen = linelen - 54;
    *(path + pathlen) = '\0';
    memset(&aligned_path, 0, sizeof(aligned_path));
    memcpy((char *)&aligned_path, path, pathlen + 1);
    if (jc_block_hash((uint64_t *)aligned_path, &path_hash, pathlen) != 0) goto error_hashdb_path_hash;

    SECS_TO_TIME(date, &mtime);
    fprintf(stderr, "file entry: [%u:%016lx] '%s', mtime %s, hashes [%u] %016lx:%016lx\n", pathlen, path_hash, path, date, hashcount, partial_hash, full_hash);

    entry = alloc_hashdb_entry(path_hash, pathlen);
    if (entry == NULL) goto error_hashdb_add;
    // init path entry items
    entry->path_hash = path_hash;
    memcpy(entry->path, aligned_path, pathlen + 1);
    entry->mtime = mtime;
    entry->partial_hash = partial_hash;
    entry->full_hash = full_hash;
    entry->hashcount = hashcount;
  }

  return 0;

error_hashdb_open:
  fprintf(stderr, "error opening hash database '%s': %s\n", dbname, strerror(errno));
  return 1;
error_hashdb_read:
  fprintf(stderr, "error reading hash database '%s': %s\n", dbname, strerror(errno));
  return 2;
error_hashdb_header:
  fprintf(stderr, "error in header of hash database '%s'\n", dbname);
  return 3;
error_hashdb_version:
  fprintf(stderr, "error: bad db version %u in hash database '%s'\n", db_ver, dbname);
  return 4;
error_hashdb_line:
  fprintf(stderr, "error: bad line in hash database '%s': '%s'\n", dbname, buf);
  return 5;
error_hashdb_add:
  fprintf(stderr, "error: internal failure allocating a hashdb entry\n");
  return 6;
error_hashdb_path_hash:
  fprintf(stderr, "error: internal failure hashing a path\n");
  return 7;
}
 
 
 /* If file hash info is already present in hash database then preload those hashes */
void load_hashdb_entry(file_t *newfile)
{
  return;
}


