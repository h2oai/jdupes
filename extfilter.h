/* jdupes extended filters
 * See jdupes.c for license information */

#ifndef JDUPES_EXTFILTER_H
#define JDUPES_EXTFILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NO_EXTFILTER

void add_extfilter(const char *option);
int extfilter_exclude(file_t * const restrict newfile);

#endif /* NO_EXTFILTER */

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_EXTFILTER_H */
