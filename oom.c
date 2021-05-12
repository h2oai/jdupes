/* Out of memory error message, the most complex C fragment in existence
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

extern void oom(const char * const restrict msg)
{
  fprintf(stderr, "\nout of memory: %s\n", msg);
  exit(EXIT_FAILURE);
}
