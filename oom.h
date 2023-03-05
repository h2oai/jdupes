/* OOM message routine header
 * Licensed either under the Creative Commons 0 license (CC0) or, for countries
 * that are sane enough to actually recognize a public domain, this is in the
 * public domain.
 */

#ifndef OOM_H
#define OOM_H

#ifdef __cplusplus
extern "C" {
#endif

extern void oom(const char * const restrict msg);
extern void nullptr(const char * restrict func);

#ifdef __cplusplus
}
#endif

#endif /* OOM_H */
