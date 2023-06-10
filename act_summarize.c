/* Print summary of match statistics to stdout
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "jdupes.h"
#include "act_summarize.h"

void summarizematches(const file_t * restrict files)
{
  unsigned int numsets = 0;
  off_t numbytes = 0;
  int numfiles = 0;

  LOUD(fprintf(stderr, "summarizematches: %p\n", files));

  while (files != NULL) {
    file_t *tmpfile;

    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
      numsets++;
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        numfiles++;
        numbytes += files->size;
        tmpfile = tmpfile->duplicates;
      }
    }
    files = files->next;
  }

  if (numsets == 0)
    printf("%s", s_no_dupes);
  else
  {
    printf("%d duplicate files (in %d sets), occupying ", numfiles, numsets);
    if (numbytes < 1000) printf("%" PRIdMAX " byte%c\n", (intmax_t)numbytes, (numbytes != 1) ? 's' : ' ');
    else if (numbytes <= 1000000) printf("%" PRIdMAX " KB\n", (intmax_t)(numbytes / 1000));
    else printf("%" PRIdMAX " MB\n", (intmax_t)(numbytes / 1000000));
  }
  return;
}
