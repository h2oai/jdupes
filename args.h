/* jdupes argument functions
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_ARGS_H
#define JDUPES_ARGS_H

#ifdef __cplusplus
extern "C" {
#endif

extern char **cloneargs(const int argc, char **argv);
int findarg(const char * const arg, const int start, const int argc, char **argv);
int nonoptafter(const char *option, const int argc, char **oldargv, char **newargv);
extern void linkfiles(file_t *files, const int linktype, const int only_current);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_ARGS_H */
