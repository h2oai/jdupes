/* jdupes progress indicator
   see jdupes.c for licensing information */

#include <stdio.h>
#include <inttypes.h>
#include "jdupes.h"
#include "likely_unlikely.h"


struct timeval time1, time2;

void update_phase1_progress(const char * const restrict type)
{
  /* Don't update progress if there is no progress to update */
  if (ISFLAG(flags, F_HIDEPROGRESS)) return;
  fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " %s (in %u specified)",
          progress, item_progress, type, user_item_count);
  fflush(stderr);
}

/* Update progress indicator if requested */
void update_phase2_progress(const char * const restrict msg, const int file_percent)
{
  static int did_fpct = 0;

  /* Don't update progress if there is no progress to update */
  if (ISFLAG(flags, F_HIDEPROGRESS)) return;

  fprintf(stderr, "\rProgress [%" PRIuMAX "/%" PRIuMAX ", %" PRIuMAX " pairs matched] %" PRIuMAX "%%",
    progress, filecount, dupecount, (progress * 100) / filecount);
  if (file_percent > -1 && msg != NULL) {
    fprintf(stderr, "  (%s: %d%%)         ", msg, file_percent);
    did_fpct = 1;
  } else if (did_fpct != 0) {
    fprintf(stderr, "                     ");
    did_fpct = 0;
  }
  fflush(stderr);
  return;
}
