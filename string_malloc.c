/*
 * String table allocator
 * A replacement for malloc() for tables of fixed strings
 *
 * Copyright (C) 2015-2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2 (your choice)
 */

#include <stdlib.h>
#include <stdint.h>

/* Must be divisible by uintptr_t */
#ifndef SMA_PAGE_SIZE
#define SMA_PAGE_SIZE 262144
#endif

static void *sma_head = NULL;
static uintptr_t *sma_lastpage = NULL;
static unsigned int sma_pages = 0;
static size_t sma_lastfree = 0;
static size_t sma_nextfree = sizeof(uintptr_t);

#ifdef DEBUG
uintmax_t sma_allocs = 0;
uintmax_t sma_free_ignored = 0;
uintmax_t sma_free_good = 0;
#endif


/* This function is for debugging the string table only! */
#if 0
static void dump_string_table(void)
{
	char *p = sma_head;
	unsigned int i = sizeof(uintptr_t);
	int pg = sma_pages;

	while (pg > 0) {
		while (i < SMA_PAGE_SIZE && *(p+i) == '\0') i++;
		printf("[%16p] (%jd) '%s'\n", p+i, strlen(p+i), p+i);
		i += strlen(p+i);
		if (pg <= 1 && i >= sma_nextfree) return;
		if (i < SMA_PAGE_SIZE) i++;
		else {
			p = (char *)*(uintptr_t *)p;
			pg--;
			i = sizeof(uintptr_t);
		}
		if (p == NULL) return;
	}

	return;
}
#endif


static inline void *string_malloc_page(void)
{
	uintptr_t * restrict pageptr;

	/* Allocate page and set up pointers at page starts */
	pageptr = (uintptr_t *)malloc(SMA_PAGE_SIZE);
	if (pageptr == NULL) return NULL;
	*pageptr = (uintptr_t)NULL;
	/* Link this page to the previous page */
	*(pageptr + sizeof(uintptr_t)) = (uintptr_t)sma_lastpage;

	/* Link previous page to this page, if applicable */
	if (sma_lastpage != NULL) *sma_lastpage = (uintptr_t)pageptr;

	/* Update last page pointers and total page counter */
	sma_lastpage = pageptr;
	sma_pages++;

	return (char *)pageptr;
}


void *string_malloc(size_t len)
{
	const char * restrict page = (char *)sma_lastpage;
	static char *address;

	/* Calling with no actual length is invalid */
	if (len < 1) return NULL;

	/* Pass-through allocations larger than page size to malloc() */
	if (len > (SMA_PAGE_SIZE - sizeof(size_t))) return malloc(len);

	/* Align objects where possible */
	if (len & (sizeof(uintptr_t) - 1)) {
		len &= ~(sizeof(uintptr_t) - 1);
		len += sizeof(uintptr_t);
	}
	/* Make room for size prefix */
	len += sizeof(size_t);

	/* Refuse to allocate a space larger than we can store */
	if (len > (unsigned int)(SMA_PAGE_SIZE - sizeof(uintptr_t))) return NULL;

	/* Initialize on first use */
	if (sma_pages == 0) {
		sma_head = string_malloc_page();
		if (!sma_head) return NULL;
		sma_nextfree = (2 * sizeof(uintptr_t));
		page = sma_head;
	}

	/* Allocate new pages when objects don't fit anymore */
	if ((sma_nextfree + len) > SMA_PAGE_SIZE) {
		page = string_malloc_page();
		if (!page) return NULL;
		sma_nextfree = (2 * sizeof(uintptr_t));
	}

	/* Allocate the space */
	address = (char *)page + sma_nextfree;
	/* Prefix object with its size */
	*(size_t *)address = (size_t)len;
	address += sizeof(size_t);
	sma_lastfree = sma_nextfree;
	sma_nextfree += len;

#ifdef DEBUG
	sma_allocs++;
#endif
	return address;
}


/* Roll back the last allocation */
void string_free(const void * restrict addr)
{
	static const char * restrict p;

	/* Do nothing on NULL address or no last length */
	if ((addr == NULL) || (sma_lastfree < sizeof(uintptr_t))) {
#ifdef DEBUG
		sma_free_ignored++;
#endif
		return;
	}

	p = (char *)sma_lastpage + sma_lastfree;

	/* Only take action on the last pointer in the page */
	addr = (void *)((uintptr_t)addr - sizeof(size_t));
	if ((uintptr_t)addr != (uintptr_t)p) {
#ifdef DEBUG
		sma_free_ignored++;
#endif
		return;
	}

	sma_nextfree = sma_lastfree;
	sma_lastfree = 0;
#ifdef DEBUG
	sma_free_good++;
#endif
	return;
}

/* Destroy all allocated pages */
void string_malloc_destroy(void)
{
	static void *cur;
	static uintptr_t *next;

	cur = (void *)sma_head;
	while (sma_pages > 0) {
		next = (uintptr_t *)*(uintptr_t *)cur;
		free(cur);
		cur = (void *)next;
		sma_pages--;
	}
	sma_head = NULL;
	return;
}
