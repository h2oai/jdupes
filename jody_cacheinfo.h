/* Detect size of CPU data caches
 * See jody_cacheinfo.c for license information */

#ifndef JODY_CACHEINFO_H
#define JODY_CACHEINFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Don't allow anything on Windows */
#ifndef ON_WINDOWS

/* Cache information structure
 * Split caches populate i/d, unified caches populate non-i/d */
struct proc_cacheinfo {
	size_t l1;
	size_t l1i;
	size_t l1d;
	size_t l2;
	size_t l2i;
	size_t l2d;
	size_t l3;
	size_t l3i;
	size_t l3d;
};

extern void get_proc_cacheinfo(struct proc_cacheinfo *pci);

#else
 #define get_proc_cacheinfo(a)
#endif /* ON_WINDOWS */

#ifdef __cplusplus
}
#endif

#endif /* JODY_CACHEINFO_H */
