/* Signal handler/interruption functions
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_INTERRUPT_H
#define JDUPES_INTERRUPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"

extern int interrupt;

void sighandler(const int signum);
#ifndef ON_WINDOWS
void sigusr1(const int signum);
void check_sigusr1(void);
#else
#define check_sigusr1()
#endif

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_INTERRUPT_H */
