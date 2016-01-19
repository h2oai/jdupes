/* Jody Bruchon's fast hashing function (headers)
 *
 * Copyright (C) 2014-2016 by Jody Bruchon <jody@jodybruchon.com>
 * See jody_hash.c for more information.
 */

#ifndef JODY_HASH_H
#define JODY_HASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Required for uint64_t */
#include <stdint.h>

/* Width of a jody_hash. Changing this will also require
 * changing the width of tail masks and endian conversion */
typedef uint64_t hash_t;

/* Version increments when algorithm changes incompatibly */
#define JODY_HASH_VERSION 4

extern hash_t jody_block_hash(const hash_t * restrict data,
		const hash_t start_hash, const size_t count);

#ifdef __cplusplus
}
#endif

#endif	/* JODY_HASH_H */
