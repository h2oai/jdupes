/* Jody Bruchon's fast hashing function
 *
 * This function was written to generate a fast hash that also has a
 * fairly low collision rate. The collision rate is much higher than
 * a secure hash algorithm, but the calculation is drastically simpler
 * and faster.
 *
 * Copyright (C) 2014-2015 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2 (your choice)
 */

#include <stdio.h>
#include <stdlib.h>
#include "jody_hash.h"

/* DO NOT modify the shift unless you know what you're doing.
 *  * This shift was decided upon after lots of testing and
 *   * changing it will likely cause lots of hash collisions. */
#define JODY_HASH_SHIFT 11

/* The salt value's purpose is to cause each byte in the
 *  * hash_t word to have a positionally dependent variation.
 *   * It is injected into the calculation to prevent a string of
 *    * identical bytes from easily producing an identical hash. */
#define JODY_HASH_SALT 0x1f3d5b79

/* The tail mask table is used for block sizes that are
 * indivisible by the width of a hash_t. It is ANDed with the
 * final hash_t-sized element to zero out data in the buffer
 * that is not part of the data to be hashed. */
static const hash_t tail_mask[] = {
	0x0000000000000000,
	0x00000000000000ff,
	0x000000000000ffff,
	0x0000000000ffffff,
	0x00000000ffffffff,
	0x000000ffffffffff,
	0x0000ffffffffffff,
	0x00ffffffffffffff,
	0xffffffffffffffff
};

/* Hash a block of arbitrary size; must be divisible by sizeof(hash_t)
 * The first block should pass a start_hash of zero.
 * All blocks after the first should pass start_hash as the value
 * returned by the last call to this function. This allows hashing
 * of any amount of data. If data is not divisible by the size of
 * hash_t, it is MANDATORY that the caller provide a data buffer
 * which is divisible by sizeof(hash_t). */
extern hash_t jody_block_hash(const hash_t * restrict data,
		const hash_t start_hash, const unsigned int count)
{
	register hash_t hash = start_hash;
	register hash_t element;
	unsigned int len;
	hash_t tail;

	/* Don't bother trying to hash a zero-length block */
	if (count == 0) return hash;

	len = count / sizeof(hash_t);
	for (; len > 0; len--) {
		element = *data;
		hash += element;
		hash += JODY_HASH_SALT;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(hash_t) * 8 - JODY_HASH_SHIFT);
		hash ^= element;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(hash_t) * 8 - JODY_HASH_SHIFT);
		hash ^= JODY_HASH_SALT;
		hash += element;
		data++;
	}

	/* Handle data tail (for blocks indivisible by sizeof(hash_t)) */
	len = count & (sizeof(hash_t) - 1);
	if (len) {
		element = *data;
		tail = element;
		tail += JODY_HASH_SALT;
		tail &= tail_mask[len];
		hash += tail;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(hash_t) * 8 - JODY_HASH_SHIFT);
		hash ^= tail;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(hash_t) * 8 - JODY_HASH_SHIFT);
		hash ^= JODY_HASH_SALT;
		hash += tail;
	}

	return hash;
}
