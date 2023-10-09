/* Argument functions
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libjodycode.h>
#include "jdupes.h"

char **cloneargs(const int argc, char **argv)
{
  static int x;
  static char **args;

  args = (char **)malloc(sizeof(char *) * (unsigned int)argc);
  if (args == NULL) jc_oom("cloneargs() start");

  for (x = 0; x < argc; x++) {
    args[x] = (char *)malloc(strlen(argv[x]) + 1);
    if (args[x] == NULL) jc_oom("cloneargs() loop");
    strcpy(args[x], argv[x]);
  }

  return args;
}


int findarg(const char * const arg, const int start, const int argc, char **argv)
{
  int x;

  for (x = start; x < argc; x++)
    if (jc_streq(argv[x], arg) == 0)
      return x;

  return x;
}

/* Find the first non-option argument after specified option. */
int nonoptafter(const char *option, const int argc, char **oldargv, char **newargv)
{
  int x;
  int targetind;
  int testind;
  int startat = 1;

  targetind = findarg(option, 1, argc, oldargv);

  for (x = optind; x < argc; x++) {
    testind = findarg(newargv[x], startat, argc, oldargv);
    if (testind > targetind) return x;
    else startat = testind;
  }

  return x;
}
