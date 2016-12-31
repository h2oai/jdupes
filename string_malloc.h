/* String table allocator
 * A replacement for malloc() for tables of fixed strings
 * See string_malloc.c for license information */

#ifndef STRING_MALLOC_H
#define STRING_MALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG
extern uintmax_t sma_allocs;
extern uintmax_t sma_free_ignored;
extern uintmax_t sma_free_good;
extern uintmax_t sma_free_scanned;
extern uintmax_t sma_free_reclaimed;
extern uintmax_t sma_free_tails;
#endif

extern void *string_malloc(size_t len);
extern void string_free(void * const restrict addr);
extern void string_malloc_destroy(void);

#ifdef __cplusplus
}
#endif

#endif	/* STRING_MALLOC_H */
