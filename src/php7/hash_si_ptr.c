/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  | This is a specialized hash map mapping uintprt_t to int32_t          |
  +----------------------------------------------------------------------+
  | Author: Oleg Grenrus <oleg.grenrus@dynamoid.com>                     |
  | Modified by Tyson Andre for fixed size, removing unused functions    |
  | See CREDITS for contributors                                         |
  +----------------------------------------------------------------------+
*/

#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "hash_ptr.h"
#include "zend.h"
#include "igbinary_bswap.h"

/* This assumes that pointers differ in low addresses rather than high addresses */
inline static uint32_t inline_hash_of_address(zend_uintptr_t ptr) {
#if UINTPTR_MAX > UINT32_MAX
	uint64_t hash = ptr;
	hash *= 0x5e2d58d8b3bce8d9;
	// This is a single assembly instruction on recent compilers/platforms
	hash = bswap_64(hash);
	return hash;
#else
	uint32_t hash = ptr;
	hash *= 0x5e2d58d9;
	// This is a single assembly instruction on recent compilers/platforms
	hash = bswap_32(hash);
	return hash;
#endif
}

/* {{{ nextpow2 */
/** Next power of 2.
 * @param n Integer.
 * @return next to n power of 2 .
 */
inline static uint32_t nextpow2(uint32_t n) {
	uint32_t m = 1;
	while (m < n) {
		m = m << 1;
	}

	return m;
}
/* }}} */
/* {{{ hash_si_ptr_init */
/**
 * @param h the pointer to the hash map that will be initialized in place
 * @param size the new capacity of the hash map
 */
int hash_si_ptr_init(struct hash_si_ptr *h, size_t size) {
	size = nextpow2(size);

	h->size = size;
	h->used = 0;
	/* Set everything to 0. sets keys to HASH_PTR_KEY_INVALID. */
	h->data = (struct hash_si_ptr_pair *)ecalloc(size, sizeof(struct hash_si_ptr_pair));
	if (h->data == NULL) {
		return 1;
	}

	return 0;
}
/* }}} */
/* {{{ hash_si_ptr_deinit */
/**
 * Frees the hash map h
 * @param h Pointer to the hash map (hash_si_ptr struct) to free internal data structures of
 */
void hash_si_ptr_deinit(struct hash_si_ptr *h) {
	efree(h->data);
	h->data = NULL;

	h->size = 0;
	h->used = 0;
}
/* }}} */
/* {{{ hash_si_ptr_rehash */
/** Rehash/resize hash_si_ptr.
 * @param h Pointer to hash_si_ptr struct.
 */
inline static void hash_si_ptr_rehash(struct hash_si_ptr *h) {
	size_t i;
	size_t old_size;
	size_t size;
	size_t mask;
	struct hash_si_ptr_pair *old_data;
	struct hash_si_ptr_pair *new_data;

	ZEND_ASSERT(h != NULL);

	/* Allocate a table with double the capacity (the next power of 2). */
	old_size = h->size;
	size = old_size * 2;
	mask = size - 1;
	old_data = h->data;
	new_data = (struct hash_si_ptr_pair *)ecalloc(size, sizeof(struct hash_si_ptr_pair));

	h->size = size;
	h->data = new_data;

	/* Copy old entries to new entries */
	for (i = 0; i < old_size; i++) {
		if (old_data[i].key != HASH_PTR_KEY_INVALID) {
			uint32_t hv = inline_hash_of_address(old_data[i].key) & mask;

			/* Do linear probing for the next free slot. */
			while (new_data[hv].key != HASH_PTR_KEY_INVALID) {
				ZEND_ASSERT(new_data[hv].key != old_data[i].key);
				hv = (hv + 1) & mask;
			}

			new_data[hv] = old_data[i];
		}
	}

	/* Free old entries */
	efree(old_data);
}
/* }}} */
/* {{{ hash_si_ptr_find_or_insert */
/**
 * @param h the pointer to the hash map.
 * @param key the key (representing to look up (or insert, if it doesn't exist)
 * @param value - If the key does not exist, this is the value to associate with key
 * @return the old value, or SIZE_MAX if the key is brand new.
 */
size_t hash_si_ptr_find_or_insert(struct hash_si_ptr *h, const zend_uintptr_t key, uint32_t value) {
	size_t size;
	size_t mask;
	uint32_t hv;

	ZEND_ASSERT(h != NULL);

	size = h->size;
	mask = size - 1;
	hv = inline_hash_of_address(key) & mask;

	while (1) {
		if (h->data[hv].key == HASH_PTR_KEY_INVALID) {
			/* This is a brand new key */
			h->data[hv].key = key;
			h->data[hv].value = value;
			h->used++;

			/* The size increased, so check if we need to expand the map */
			if ((h->size >> 1) < h->used) {
				hash_si_ptr_rehash(h);
			}
			return SIZE_MAX;
		} else if (h->data[hv].key == key) {
			/* This already exists in the hash map */
			return h->data[hv].value;
		}
		/* linear prob */
		hv = (hv + 1) & mask;
	}
}
/* }}} */
/* {{{ hash_si_ptr_size */
/**
 * @param h the hash map from pointers to integers.
 * @return the number of elements in this hash map.
 */
size_t hash_si_ptr_size(struct hash_si_ptr *h) {
	assert(h != NULL);

	return h->used;
}
/* }}} */
/* {{{ hash_si_ptr_capacity */
/**
 * @param h the hash map from pointers to integers.
 * @return the capacity of this hash map.
 */
size_t hash_si_ptr_capacity(struct hash_si_ptr *h) {
	assert(h != NULL);

	return h->size;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 2
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
