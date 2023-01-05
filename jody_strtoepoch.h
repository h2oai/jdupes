/* Jody Bruchon's datetime-to-epoch conversion function
 *
 * Copyright (C) 2020-2023 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */

#ifndef JODY_STRTOEPOCH_H
#define JODY_STRTOEPOCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* See note in jody_strtoepoch.c about NO_EXTFILTER */
#ifndef NO_EXTFILTER
time_t strtoepoch(const char * const datetime);
#endif /* NO_EXTFILTER */

#ifdef __cplusplus
}
#endif

#endif /* JODY_STRTOEPOCH_H */
