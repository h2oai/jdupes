/* Jody Bruchon's Windows Unicode helper routines
 * See jody_win_unicode.c for license information */

#ifndef JODY_WIN_UNICODE_H
#define JODY_WIN_UNICODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

extern int fwprint(FILE * const restrict stream, const char * const restrict str, const int cr);

#ifdef UNICODE
 #ifndef WPATH_MAX
  #define WPATH_MAX 8192
 #endif
 #ifndef M2W
  #define M2W(a,b) MultiByteToWideChar(CP_UTF8, 0, a, -1, (LPWSTR)b, WPATH_MAX)
 #endif
 #ifndef W2M
  #define W2M(a,b) WideCharToMultiByte(CP_UTF8, 0, a, -1, (LPSTR)b, WPATH_MAX, NULL, NULL)
 #endif
 extern void slash_convert(char *path);
 extern void widearg_to_argv(int argc, wchar_t **wargv, char **argv);
#else
 #define slash_convert(a)
#endif /* UNICODE */

#ifdef __cplusplus
}
#endif

#endif /* JODY_WIN_UNICODE_H */
