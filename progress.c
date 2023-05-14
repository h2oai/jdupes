/* jdupes progress indicator
   see jdupes.c for licensing information */

#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include "jdupes.h"


/* Update progress indicator if requested */
void update_progress(const char * const restrict msg, const int file_percent)
{
  static int did_fpct = 0;

#ifndef ON_WINDOWS
  /* Notify of change to soft abort status if SIGUSR1 received */
  if (usr1_toggle != 0) {
    fprintf(stderr, "\njdupes received a USR1 signal; soft abort (-Z) is now %s\n", usr1_toggle == 1 ? "ON" : "OFF" );
    usr1_toggle = 0;
  }
#endif

  /* The caller should be doing this anyway...but don't trust that they did */
  if (ISFLAG(flags, F_HIDEPROGRESS)) return;

  gettimeofday(&time2, NULL);

  if (progress == 0 || time2.tv_sec > time1.tv_sec) {
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
