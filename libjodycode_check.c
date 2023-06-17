/* libjodycode version checks
 *
 * Code to embed the libjodycode version info and check against the currently
 * linked libjodycode to check for and report incompatibilities
 *
 * Copyright (C) 2023 by Jody Bruchon <jody@jodybruchon.com>
 * Licensed under The MIT License */

#include <stdio.h>
#include <stdlib.h>
#include <libjodycode.h>

const char *jc_build_version = LIBJODYCODE_VER;
const int jc_build_api_version = LIBJODYCODE_API_VERSION;
const int jc_build_api_featurelevel = LIBJODYCODE_API_FEATURE_LEVEL;

/* API sub-version info array, terminated with 255
 * For any API you don't use, comment out the API name and replace it
 * with '0,' instead. */
const unsigned char jc_build_api_versiontable[] = {
	LIBJODYCODE_CACHEINFO_VER,
	LIBJODYCODE_JODY_HASH_VER,
	LIBJODYCODE_OOM_VER,
	LIBJODYCODE_PATHS_VER,
	LIBJODYCODE_SIZE_SUFFIX_VER,
	LIBJODYCODE_SORT_VER,
	LIBJODYCODE_STRING_VER,
	LIBJODYCODE_STRTOEPOCH_VER,
	LIBJODYCODE_WIN_STAT_VER,
	LIBJODYCODE_WIN_UNICODE_VER,
	LIBJODYCODE_ERROR_VER,
	LIBJODYCODE_ALARM_VER,
	255
};

const char *jc_versiontable_section[] = {
	"cacheinfo",
	"jody_hash",
	"oom",
	"paths",
	"size_suffix",
	"sort",
	"string",
	"strtoepoch",
	"win_stat",
	"win_unicode",
	"error",
	"alarm",
	NULL
};


int libjodycode_version_check(int verbose, int bail)
{
	const unsigned char * const restrict build = jc_build_api_versiontable;
	const unsigned char * const restrict lib = jc_api_versiontable;
	int i = 0;

	/* Force a version dump if requested */
	if (verbose > 1) goto incompatible_versiontable;
	while (build[i] != 255) {
		if (build[i] != 0 && (lib[i] == 0 || build[i] != lib[i])) goto incompatible_versiontable;
		i++;
	}
	return 0;

incompatible_versiontable:
	if (verbose) {
		fprintf(stderr, "\n==============================================================================\n");
		fprintf(stderr,   "internal error: libjodycode on this system is an incompatible version\n\n");
		fprintf(stderr, "Currently using libjodycode v%s, API %d, feature level %d\n",
				jc_version, jc_api_version, jc_api_featurelevel);
		fprintf(stderr, "  Built against libjodycode v%s, API %d, feature level %d\n\n",
				jc_build_version, jc_build_api_version, jc_build_api_featurelevel);
		if (lib[i] == 0) fprintf(stderr, "API sections are missing in libjodycode; it's probably too old.\n");
		else fprintf(stderr, "The first incompatible API section found is '%s' (want v%d, got v%d).\n",
				jc_versiontable_section[i], build[i], lib[i]);
		fprintf(stderr, "==============================================================================\n\n");
		fprintf(stderr, "\nUpdate libjodycode on your system and try again. If you continue to get this\n");
		fprintf(stderr, "error, contact the package or distribution maintainer. If all else fails, send\n");
		fprintf(stderr, "an email to jody@jodybruchon.com for help (but only as a last resort, please.)\n\n");
	}
	if (bail) exit(EXIT_FAILURE);
	return 1;
}

#ifdef JC_TEST
int main(void)
{
	libjodycode_version_check(2, 0);
	return 0;
}
#endif
