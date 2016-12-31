/* Jody Bruchon's sorting code library
 *
 * Copyright (C) 2014-2017 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */

#include "jody_sort.h"

#define IS_NUM(a) (((a >= '0') && (a <= '9')) ? 1 : 0)

extern int numeric_sort(const char * restrict c1,
                const char * restrict c2, int sort_direction)
{
  int len1 = 0, len2 = 0;
  int precompare = 0;

  /* Numerically correct sort */
  while (*c1 != '\0' && *c2 != '\0') {
    /* Reset string length counters */
    len1 = 0; len2 = 0;

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
    }

    /* Do normal comparison */
    if (*c1 == *c2) {
      c1++; c2++;
      len1++; len2++;
    /* Put symbols and spaces after everything else */
    } else if (*c2 < '.' && *c1 >= '.') return -sort_direction;
    else if (*c1 < '.' && *c2 >= '.') return sort_direction;
    /* Normal strcmp() style compare */
    else if (*c1 > *c2) return sort_direction;
    else return -sort_direction;
  }

  /* Longer strings generally sort later */
  if (len1 < len2) return -sort_direction;
  if (len1 > len2) return sort_direction;
  /* Normal strcmp() style comparison */
  if (*c1 == '\0' && *c2 != '\0') return -sort_direction;
  if (*c1 != '\0' && *c2 == '\0') return sort_direction;

  /* Fall through: the strings are equal */
  return 0;
}
