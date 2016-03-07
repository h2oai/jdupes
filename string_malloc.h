/*
 * String table allocator
 * A replacement for malloc() for tables of fixed strings
 *
 * Copyright (C) 2015-2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2 (your choice)
 */

#ifndef STRING_MALLOC_H
#define STRING_MALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG
extern uintmax_t sma_allocs;
extern uintmax_t sma_free_ignored;
extern uintmax_t sma_free_good;
#endif

void *string_malloc(size_t len);
void string_free(const void * restrict addr);
void string_malloc_destroy(void);

#ifdef __cplusplus
}
#endif

#endif	/* STRING_MALLOC_H */
