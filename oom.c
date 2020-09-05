/* Out of memory error message
 * Distributed under The MIT License
 * (as if anything so trivial deserves a license)
 * By Jody Bruchon <jody@jodybruchon.com>
 * (as if anything so trivial will serve my ego)
 */

#include "oom.h"

extern void oom(const char * const restrict msg)
{
  fprintf(stderr, "\nout of memory: %s\n", msg);
  string_malloc_destroy();
  exit(EXIT_FAILURE);
}
