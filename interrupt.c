/* Signal handler/interruption functions
 * This file is part of jdupes; see jdupes.c for license information */

#ifdef ON_WINDOWS
#define _WIN32_WINNT 0x0500
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "likely_unlikely.h"
#include "jdupes.h"

int interrupt = 0;

#ifdef ON_WINDOWS
HANDLE hTimer;
#else
static int usr1_toggle = 0;
#endif /* ON_WINDOWS */

/* Catch CTRL-C and either notify or terminate */
void sighandler(const int signum)
{
  (void)signum;
  if (interrupt || !ISFLAG(flags, F_SOFTABORT)) {
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
  }
  interrupt = 1;
  return;
}


#ifdef ON_WINDOWS
void CALLBACK catch_win_alarm(PVOID arg1, BOOLEAN arg2)
{
  (void)arg1; (void)arg2;
  progress_alarm = 1;
  return;
}


void start_progress_alarm(void)
{
  LOUD(fprintf(stderr, "start_win_alarm()\n");)
  if (!CreateTimerQueueTimer(&hTimer, NULL, (WAITORTIMERCALLBACK)catch_win_alarm, 0, 1000, 1000, 0))
    goto error_createtimer;
  progress_alarm = 1;
  return;
error_createtimer:
  printf("CreateTimerQueueTimer failed with error %lu\n", GetLastError());
  exit(EXIT_FAILURE);
}


void stop_progress_alarm(void)
{
  LOUD(fprintf(stderr, "stop_win_alarm()\n");)
  CloseHandle(hTimer);
  return;
}

#else /* not ON_WINDOWS */

void start_progress_alarm(void)
{
  signal(SIGALRM, catch_sigalrm);
  alarm(1);
  return;
}


void stop_progress_alarm(void)
{
  signal(SIGALRM, SIG_IGN);
  alarm(0);
  return;
}


void catch_sigalrm(const int signum)
{
  (void)signum;
  progress_alarm = 1;
  alarm(1);
  return;
}
#endif /* ON_WINDOWS */


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
