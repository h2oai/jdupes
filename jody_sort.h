/* Jody Bruchon's sorting code library
 *
 * Copyright (C) 2014-2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2 (your choice)
 */

#ifndef JODY_SORT_H
#define JODY_SORT_H

#ifdef __cplusplus
extern "C" {
#endif

extern int numeric_sort(const char * restrict c1,
                const char * restrict c2, int sort_direction);

#ifdef __cplusplus
}
#endif

#endif /* JODY_SORT_H */
