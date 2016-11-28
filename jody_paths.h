/* Jody Bruchon's path manipulation code library
 *
 * Copyright (C) 2014-2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2 (your choice)
 */

#ifndef JODY_PATHS_H
#define JODY_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PATHBUF_SIZE
 #define PATHBUF_SIZE 4096
#endif

extern int collapse_dotdot(char * const path);
extern int make_relative_link_name(const char * const src,
                const char * const dest, char * rel_path);

#ifdef __cplusplus
}
#endif

#endif /* JODY_PATHS_H */
