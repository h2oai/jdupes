/* File hash database management
 * This file is part of jdupes; see jdupes.c for license information */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "jdupes.h"
#include "libjodycode.h"
#include "likely_unlikely.h"
#include "hashdb.h"
#include "version.h"

int hash_algo = 0;
uint64_t flags = 0;

int main(int argc, char **argv)
{
  const char * const default_name = "jdupes_hashdb.txt";
  const char *dbname, *action;
  int64_t hdbsize;
  uint64_t cnt;

  if (argc != 3) goto util_usage;

  dbname = argv[1];
  action = argv[2];

  if (strcmp(dbname, ".") == 0) dbname = default_name;
  hdbsize = load_hash_database(dbname);
  if (hdbsize < 0) goto error_load_hashdb;
  if (hdbsize > 0 && !ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "%" PRId64 " entries loaded.\n", hdbsize);

  fprintf(stderr, "name %s, action %s\n", dbname, action);
  if (strcmp(action, "dump") == 0) {
    dump_hashdb();
    return 0;
  } else if (strcmp(action, "clean") == 0) {
    fprintf(stderr, "Cleaning entries\n");
    if (cleanup_hashdb(&cnt, NULL) != 0) goto error_hashdb_cleanup;
  } else goto error_action;

  return 0;

util_usage:
  printf("jdupes hashdb utility %s (%s)\n", VER, VERDATE);
  printf("usage: %s hash_database_name action\n", argv[0]);
  printf("If the name is a period '.' then 'jdupes_hashdb.txt' will be used\n");
  printf("Actions: none yet\n");
  exit(EXIT_FAILURE);
error_hashdb_cleanup:
  fprintf(stderr, "error cleaning up hash database '%s'\n", dbname);
  exit(EXIT_FAILURE);
error_load_hashdb:
  fprintf(stderr, "error: cannot open hash database '%s'\n", dbname);
  exit(EXIT_FAILURE);
error_action:
  fprintf(stderr, "error: unknown action '%s'\n", action);
  exit(EXIT_FAILURE);
}
