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
#include "libjodycode_check.h"

#ifdef JC_TEST
 #define JC_TEST_ONLY(a) a
#else
 #define JC_TEST_ONLY(a)
#endif

const char *jc_build_version = LIBJODYCODE_VER;
const int jc_build_api_version = LIBJODYCODE_API_VERSION;
const int jc_build_api_featurelevel = LIBJODYCODE_API_FEATURE_LEVEL;
const int jc_build_windows_unicode = LIBJODYCODE_WINDOWS_UNICODE;
const int jc_build_min_featurelevel = MY_FEATURELEVEL_REQ;

int libjodycode_version_check(int verbose, int bail)
{
	JC_TEST_ONLY(if (verbose > 1) fprintf(stderr, "libjodycode version check test code\n\n");)
	JC_TEST_ONLY(if (verbose > 1) goto incompatible_version;)
	if (jc_build_api_version != jc_api_version) goto incompatible_version;
	if (jc_build_min_featurelevel > jc_api_featurelevel) goto incompatible_version;
	if (jc_build_windows_unicode != jc_windows_unicode) goto incompatible_version;
	return 0;

incompatible_version:
	if (verbose) {
		fprintf(stderr, "\n==============================================================================\n");
		fprintf(stderr,   "internal error: libjodycode on this system is an incompatible version\n\n");
		fprintf(stderr, "Currently using libjodycode v%s, API %d, feature level %d\n",
				jc_version, jc_api_version, jc_api_featurelevel);
		fprintf(stderr, "  Built against libjodycode v%s, API %d, feature level %d\n\n",
				jc_build_version, jc_build_api_version, jc_build_api_featurelevel);
		if (jc_windows_unicode != jc_build_windows_unicode)
			fprintf(stderr, "libjodycode was built with%s Windows Unicode but %sUnicode is required.\n\n",
					jc_windows_unicode == 1 ? "" : "out",
					jc_build_windows_unicode == 1 ? "" : "non-");
		if (jc_build_min_featurelevel > jc_build_api_featurelevel)
			fprintf(stderr, "libjodycode feature level >= %d is required but linked library is level %d\n\n",
				jc_build_min_featurelevel, jc_build_api_featurelevel);
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
