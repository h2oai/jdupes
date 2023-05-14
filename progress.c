/* jdupes progress indicator
   see jdupes.c for licensing information */

#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include "jdupes.h"
#include "likely_unlikely.h"


void update_phase1_progress(const uintmax_t flag, const char * const restrict type)
{
  /* Don't update progress if there is no progress to update */
  if (ISFLAG(flags, F_HIDEPROGRESS)) return;
  gettimeofday(&time2, NULL);
  if (unlikely(flag == 0 || time2.tv_sec > time1.tv_sec)) {
    fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " %s (in %u specified)",
            progress, item_progress, type, user_item_count);
  }
  time1.tv_sec = time2.tv_sec;
}

/* Update progress indicator if requested */
void update_phase2_progress(const char * const restrict msg, const int file_percent)
{
  static int did_fpct = 0;

  /* Don't update progress if there is no progress to update */
  if (ISFLAG(flags, F_HIDEPROGRESS)) return;

  gettimeofday(&time2, NULL);

  if (unlikely(progress == 0 || time2.tv_sec > time1.tv_sec)) {
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
  }
  time1.tv_sec = time2.tv_sec;
  return;
}
