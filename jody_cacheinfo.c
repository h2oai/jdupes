/* Detect and report size of CPU caches
 *
 * Copyright (C) 2018 by Jody Bruchon <jody@jodybruchon.com>
 * Distributed under The MIT License
 *
 * If an error occurs or a cache is missing, zeroes are returned
 * Unified caches populate l1/l2/l3; split caches populate lXi/lXd instead
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jody_cacheinfo.h"

/* None of this code is useful on Windows, don't build anything there */
#ifndef ON_WINDOWS

static char *pathidx;
static char buf[16];
static char path[64] = "/sys/devices/system/cpu/cpu0/cache/index";


/*** End declarations, begin code ***/


/* Linux sysfs */
static size_t read_procfile(const char * const restrict name)
{
	FILE *fp;
	size_t i;

	if (name == NULL) return 0;

	memset(buf, 0, 16);
	/* Create path */
	*pathidx = '\0';
	strcpy(pathidx, name);
	fp = fopen(path, "rb");
	if (fp == NULL) return 0;
	i = fread(buf, 1, 16, fp);
	if (ferror(fp)) return 0;
	fclose(fp);
	return i;
}


void get_proc_cacheinfo(struct proc_cacheinfo *pci)
{
	char *idx;
	size_t i;
	size_t size;
	int level;
	char type;
	char index;

	if (pci == NULL) return;
	memset(pci, 0, sizeof(struct proc_cacheinfo));
	i = strlen(path);
	if (i > 48) return;
	idx = path + i;
	pathidx = idx + 1;
	*pathidx = '/'; pathidx++;

	for (index = '0'; index < '9'; index++) {
		*idx = index;

		/* Get the level for this index */
		if (read_procfile("level") == 0) break;
		if (*buf < '1' || *buf > '3') break;
		else level = (*buf) + 1 - '1';

		/* Get the size */
		if (read_procfile("size") == 0) break;
		size = (size_t)atoi(buf) * 1024;
		if (size == 0) break;

		/* Get the type */
		if (read_procfile("type") == 0) break;
		if (*buf != 'U' && *buf != 'I' && *buf != 'D') break;
		type = *buf;

		/* Act on it */
		switch (type) {
			case 'D':
			switch (level) {
				case 1: pci->l1d = size; break;
				case 2: pci->l2d = size; break;
				case 3: pci->l3d = size; break;
				default: return;
			};
			break;
			case 'I':
			switch (level) {
				case 1: pci->l1i = size; break;
				case 2: pci->l2i = size; break;
				case 3: pci->l3i = size; break;
				default: return;
			};
			break;
			case 'U':
			switch (level) {
				case 1: pci->l1 = size; break;
				case 2: pci->l2 = size; break;
				case 3: pci->l3 = size; break;
				default: return;
			};
			break;
			default: return;
		}

		/* Continue to next index */
	}
	return;
}

#endif /* ON_WINDOWS */


/* This is for testing only */
#if 0
int main(void)
{
	static struct proc_cacheinfo pci;
	get_proc_cacheinfo(&pci);

	printf("Cache: L1 %d,%d,%d  L2 %d,%d,%d L3 %d,%d,%d\n",
		pci.l1, pci.l1i, pci.l1d,
		pci.l2, pci.l2i, pci.l2d,
		pci.l3, pci.l3i, pci.l3d);
	return 0;
}
#endif
