/* Out of memory error messages, the most complex C functions ever made
 * Licensed either under the Creative Commons 0 license (CC0) or, for countries
 * that are sane enough to actually recognize a public domain, this is in the
 * public domain.
 *
 * (It was under The MIT License, as if anything so trivial deserves a license)
 *
 * By Jody Bruchon <jody@jodybruchon.com>
 * (as if anything so trivial will serve my ego)
 */

#include <stdio.h>
#include <stdlib.h>
#include "oom.h"

/* Out of memory failure */
extern void oom(const char * const restrict msg)
{
  fprintf(stderr, "\nout of memory: %s\n", msg);
  exit(EXIT_FAILURE);
}

/* Null pointer failure */
extern void nullptr(const char * restrict func)
{
  static const char n[] = "(NULL)";
  if (func == NULL) func = n;
  fprintf(stderr, "\ninternal error: NULL pointer passed to %s\n", func);
  exit(EXIT_FAILURE);
}
