/* Signal handler/interruption functions
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <libjodycode.h>
#include "likely_unlikely.h"
#include "jdupes.h"

/* CTRL-C */
int interrupt = 0;

#ifndef ON_WINDOWS
static int usr1_toggle = 0;
#endif

/* Catch CTRL-C and either notify or terminate */
void catch_interrupt(const int signum)
{
  (void)signum;
  interrupt = 1;
  exit_status = EXIT_FAILURE;
  return;
}


/* SIGUSR1 for -Z toggle; not available on Windows */
#ifndef ON_WINDOWS
void catch_sigusr1(const int signum)
{
  (void)signum;
  if (!ISFLAG(flags, F_SOFTABORT)) {
    SETFLAG(flags, F_SOFTABORT);
    usr1_toggle = 1;
  } else {
    CLEARFLAG(flags, F_SOFTABORT);
    usr1_toggle = 2;
  }
  return;
}


void check_sigusr1(void)
{
  /* Notify of change to soft abort status if SIGUSR1 received */
  if (unlikely(usr1_toggle != 0)) {
    fprintf(stderr, "\njdupes received a USR1 signal; soft abort (-Z) is now %s\n", usr1_toggle == 1 ? "ON" : "OFF" );
    usr1_toggle = 0;
  }
  return;
}
#else
#define check_sigusr1()
#endif
