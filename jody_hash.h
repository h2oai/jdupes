/* Jody Bruchon's fast hashing function (headers)
 *
 * Copyright (C) 2014-2015 by Jody Bruchon <jody@jodybruchon.com>
 * See jody_hash.c for more information.
 */

#ifndef JODY_HASH_H
#define JODY_HASH_H

/* Required for uint64_t */
#include <stdint.h>

/* Width of a jody_hash. Changing this will also require
 * changing the width of tail masks and endian conversion */
typedef uint64_t hash_t;

/* Version increments when algorithm changes incompatibly */
#define JODY_HASH_VERSION 1

/* DO NOT modify the shift unless you know what you're doing.
 * This shift was decided upon after lots of testing and
 * changing it will likely cause lots of hash collisions. */
#define JODY_HASH_SHIFT 11

/* The salt value's purpose is to cause each byte in the
 * hash_t word to have a positionally dependent variation.
 * It is injected into the calculation to prevent a string of
 * identical bytes from easily producing an identical hash. */
#define JODY_HASH_SALT 0x1f3d5b79

extern hash_t jody_block_hash(const hash_t * restrict data,
		const hash_t start_hash, const unsigned int count);

#endif	/* JODY_HASH_H */
