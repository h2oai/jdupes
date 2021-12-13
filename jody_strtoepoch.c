/* Jody Bruchon's datetime-to-epoch conversion function
 *
 * Copyright (C) 2020-2021 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */

#include <string.h>
#include <sys/time.h>
#include <time.h>

#define REQ_NUM(a) { if (a < '0' || a > '9') return -1; }
#define ATONUM(a,b) (a = b - '0')
/* Fast multiplies by 100 (*64 + *32 + *4) and 10 (*8 + *2) */
#ifndef STRTOEPOCH_USE_REAL_MULTIPLY
 #define MUL100(a) ((a << 6) + (a << 5) + (a << 2))
 #define MUL10(a) ((a << 3) + a + a)
#else
 #define MUL100(a) (a * 100)
 #define MUL10(a) (a * 10)
#endif /* STRTOEPOCH_USE_REAL_MULTIPLY */

/* Accepts date[time] strings "YYYY-MM-DD" or "YYYY-MM-DD HH:MM:SS"
 * and returns the number of seconds since the Unix Epoch a la mktime()
 * or returns -1 on any error */
time_t strtoepoch(const char * const datetime)
{
	time_t secs = 0;  /* 1970-01-01 00:00:00 */
	const char * restrict p = datetime;
	int i;
	struct tm tm;

	if (datetime == NULL || *datetime == '\0') return -1;
	memset(&tm, 0, sizeof(struct tm));

	/* This code replaces "*10" with shift<<3 + add + add */
	/* Process year */
	tm.tm_year = 1000;
	REQ_NUM(*p); if (*p == '2') tm.tm_year = 2000; p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_year += MUL100(i); p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_year += MUL10(i); p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_year += i; p++;
	tm.tm_year -= 1900;  /* struct tm year is since 1900 */
	if (*p != '-') return -1;
	p++;
	/* Process month (0-11, not 1-12) */
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_mon = MUL10(i); p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_mon += (i - 1); p++;
	if (*p != '-') return -1;
	p++;
	/* Process day */
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_mday = MUL10(i); p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_mday += i; p++;
	/* If YYYY-MM-DD is specified only, skip the time part */
	if (*p == '\0') goto skip_time;
	if (*p != ' ') return -1; else p++;
	/* Process hours */
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_hour = MUL10(i); p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_hour += i; p++;
	if (*p != ':') return -1;
	p++;
	/* Process minutes */
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_min = MUL10(i); p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_min += i; p++;
	if (*p != ':') return -1;
	p++;
	/* Process seconds */
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_sec = MUL10(i); p++;
	REQ_NUM(*p); ATONUM(i, *p); tm.tm_sec += i; p++;
	/* Junk after datetime string should cause an error */
	if (*p != '\0') return -1;
skip_time:
	tm.tm_isdst = -1;  /* Let the host library decide if DST is in effect */
	secs = mktime(&tm);
	return secs;
}
