/* Jody Bruchon's sorting code library
 *
 * Copyright (C) 2014-2022 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */

#ifndef NO_NUMSORT

#include <stdlib.h>
#include "jody_sort.h"

#define IS_NUM(a) (((a >= '0') && (a <= '9')) ? 1 : 0)
#define IS_LOWER(a) (((a >= 'a') && (a <= 'z')) ? 1 : 0)

extern int numeric_sort(char * restrict c1,
                char * restrict c2, int sort_direction)
{
  int len1 = 0, len2 = 0;
  int precompare;
  char *rewind1, *rewind2;

  if (c1 == NULL || c2 == NULL) return -99;

  /* Numerically correct sort */
  while (*c1 != '\0' && *c2 != '\0') {
    /* Reset string length counters and rewind points */
    len1 = 0; len2 = 0; rewind1 = c1; rewind2 = c2;

    /* Skip all sequences of zeroes */
    while (*c1 == '0') {
      len1++;
      c1++;
    }
    while (*c2 == '0') {
      len2++;
      c2++;
    }

    /* If both chars are numeric, do a numeric comparison */
    if (IS_NUM(*c1) && IS_NUM(*c2)) {
      precompare = 0;

      /* Scan numbers and get preliminary results */
      while (IS_NUM(*c1) && IS_NUM(*c2)) {
        if (*c1 < *c2) precompare = -sort_direction;
        if (*c1 > *c2) precompare = sort_direction;
        len1++; len2++;
        c1++; c2++;

        /* Skip remaining digit pairs after any
         * difference is found */
        if (precompare != 0) {
          while (IS_NUM(*c1) && IS_NUM(*c2)) {
            len1++; len2++;
            c1++; c2++;
          }
          break;
        }
      }

      /* One numeric and one non-numeric means the
       * numeric one is larger and sorts later */
      if (IS_NUM(*c1) ^ IS_NUM(*c2)) {
        if (IS_NUM(*c1)) return sort_direction;
        else return -sort_direction;
      }

      /* If the last test fell through, numbers are
       * of equal length. Use the precompare result
       * as the result for this number comparison. */
      if (precompare != 0) return precompare;
    } else {
      /* Zeroes aren't followed by a digit; rewind the streams */
      c1 = rewind1; c2 = rewind2;
      len1 = 0; len2 = 0;
    }

    /* Do normal comparison */
    if (*c1 == *c2 && *c1 != '\0' && *c2 != '\0') {
      c1++; c2++;
      len1++; len2++;
    /* Put symbols and spaces after everything else */
    } else if (*c2 < '.' && *c1 >= '.') return -sort_direction;
    else if (*c1 < '.' && *c2 >= '.') return sort_direction;
    /* Normal strcasecmp() style compare */
    else {
      char s1 = *c1, s2 = *c2;
      /* Convert lowercase into uppercase */
      if (IS_LOWER(s1)) s1 = (char)(s1 - 32);
      if (IS_LOWER(s2)) s2 = (char)(s2 - 32);
      if (s1 > s2) return sort_direction;
      else return -sort_direction;
    }
  }

  /* Longer strings generally sort later */
  if (len1 < len2) return -sort_direction;
  if (len1 > len2) return sort_direction;

  /* Normal comparison - FIXME? length check should already handle these */
  if (*c1 == '\0' && *c2 != '\0') return -sort_direction;
  if (*c1 != '\0' && *c2 == '\0') return sort_direction;

  /* Fall through: the strings are equal */
  return 0;
}

#endif /* NO_NUMSORT */
