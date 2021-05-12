/* Jody Bruchon's datetime-to-epoch conversion function
 *
 * Copyright (C) 2020-2021 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */

#ifndef JODY_STRTOEPOCH_H
#define JODY_STRTOEPOCH_H

#ifdef __cplusplus
extern "C" {
#endif

time_t strtoepoch(const char * const datetime);

#ifdef __cplusplus
}
#endif

#endif /* JODY_STRTOEPOCH_H */
