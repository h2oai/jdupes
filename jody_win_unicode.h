/* Jody Bruchon's Windows Unicode helper routines
 *
 * Copyright (C) 2014-2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2 (your choice)
 */

#ifndef JODY_WIN_UNICODE_H
#define JODY_WIN_UNICODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"

#ifdef UNICODE
extern void slash_convert(char *path);
extern void widearg_to_argv(int argc, wchar_t **wargv, char **argv);
extern int fwprint(FILE * const restrict stream, const char * const restrict str, const int cr);
#else
 #define fwprint(a,b,c) fprintf(a, "%s%s", b, c ? "\n" : "")
 #define slash_convert(a)
#endif /* UNICODE */

#ifdef __cplusplus
}
#endif

#endif /* JODY_WIN_UNICODE_H */
