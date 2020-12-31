/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Author: Oleg Grenrus <oleg.grenrus@dynamoid.com>                     |
  | See CREDITS for contributors                                         |
  +----------------------------------------------------------------------+
*/

#ifdef PHP_WIN32
# include "ig_win32.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "hash.h"
#include "zend.h"

/* {{{ nextpow2 */
/** Next power of 2.
 * @param n Integer.
 * @return the smallest power of 2 that is >= n.
 */
inline static uint32_t nextpow2(uint32_t n) {
	uint32_t m = 1;
	while (m < n) {
		m = m << 1;
	}

	return m;
}
/* }}} */
/* {{{ hash_si_init */
/**
 * Initializes a hash_si value with the given capacity
 */
int hash_si_init(struct hash_si *h, uint32_t size) {
	size = nextpow2(size);

	h->mask = size - 1;
	h->used = 0;
	h->data = (struct hash_si_pair *)ecalloc(size, sizeof(struct hash_si_pair));
	if (h->data == NULL) {
		return 1;
	}

	return 0;
}
/* }}} */
/* {{{ hash_si_deinit */
void hash_si_deinit(struct hash_si *h) {
	size_t i;
	const size_t mask = h->mask;
	struct hash_si_pair *const data = h->data;

	for (i = 0; i <= mask; i++) {
		if (data[i].key_zstr != NULL) {
			zend_string_release(data[i].key_zstr);
		}
	}

	efree(data);
}
/* }}} */
/* {{{ get_key_hash */
inline static uint32_t get_key_hash(zend_string *key_zstr) {
	uint32_t key_hash = ZSTR_HASH(key_zstr);
#if SIZEOF_ZEND_LONG > 4
	if (UNEXPECTED(key_hash == 0)) {
		/* A key_hash of uint32_t(0) would be treated like a gap when inserted. Change the hash used to 1 instead. */
		/* uint32_t(ZSTR_HASH) is 0 for 1 in 4 billion - optimized builds may use cmove so there are no branch mispredicts, changing to key_hash >> 32 doesn't speed up benchmark/serialize-stringarray */
		return 1;
	}
#endif
	return key_hash;
}
/* }}} */
/* {{{ hash_si_rehash */
/** Rehash/resize hash_si.
 * @param h Pointer to hash_si struct.
 */
inline static void hash_si_rehash(struct hash_si *h) {
	size_t i;
	size_t old_size;
	size_t new_size;
	size_t new_mask;
	struct hash_si_pair *old_data;
	struct hash_si_pair *new_data;

	/* Allocate a table with double the capacity (the next power of 2). */
	ZEND_ASSERT(h != NULL);
	old_size = h->mask + 1;
	new_size = old_size * 2;
	new_mask = new_size - 1;

	old_data = h->data;
	new_data = (struct hash_si_pair *)ecalloc(new_size, sizeof(struct hash_si_pair));
	h->data = new_data;
	h->mask = new_mask;

	/* Copy old entries to new entries */
	for (i = 0; i < old_size; i++) {
		const struct hash_si_pair *old_pair = &old_data[i];
		if (old_pair->key_zstr != NULL) {
			uint32_t hv = old_pair->key_hash & new_mask;
			/* We already computed the hash, avoid recomputing it. */
			/* Do linear probing for the next free slot. */
			while (new_data[hv].key_hash != 0) {
				hv = (hv + 1) & new_mask;
			}
			new_data[hv] = old_data[i];
		}
	}

	/* Free old entries */
	efree(old_data);
}
/* }}} */
/**
 * If the string key already exists in the map, return the associated value.
 * If it doesn't exist, indicate that to the caller.
 */
struct hash_si_result hash_si_find_or_insert(struct hash_si *h, zend_string *key_zstr, uint32_t value) {
	struct hash_si_result result;
	struct hash_si_pair *pair;
	struct hash_si_pair *data;
	uint32_t key_hash = get_key_hash(key_zstr);
	size_t mask;
	uint32_t hv;

	ZEND_ASSERT(h != NULL);

	mask = h->mask;
	hv = key_hash & mask;
	data = h->data;

	while (1) {
		pair = &data[hv];
		if (pair->key_hash == 0) {
			/* This is a brand new key */
			pair->key_zstr = key_zstr;
			pair->key_hash = key_hash;
			pair->value = value;
			h->used++;

			/* The size increased, so check if we need to expand the map */
			if (h->mask * 3 / 4 < h->used) {
				hash_si_rehash(h);
			}
			result.code = hash_si_code_inserted;
			zend_string_addref(key_zstr);
			return result;
		} else if (pair->key_hash == key_hash && EXPECTED(zend_string_equals(pair->key_zstr, key_zstr))) {
			/* This already exists in the hash map */
			result.code = hash_si_code_exists;
			result.value = pair->value;
			return result;
		}
		/* linear prob */
		hv = (hv + 1) & mask;
	}
}
/* }}} */
/* {{{ hash_si_traverse */
/*
void hash_si_traverse(struct hash_si *h, int (*traverse_function) (const char *key, size_t key_len, uint32_t value)) {
	size_t i;

	assert(h != NULL && traverse_function != NULL);

	for (i = 0; i < h->size; i++) {
		if (h->data[i].key != NULL && traverse_function(h->data[i].key, h->data[i].key_len, h->data[i].value) != 1) {
			return;
		}
	}
}
*/
/* }}} */
/* {{{ hash_si_size */
/** Returns the number of elements in the hash map h. */
size_t hash_si_size(struct hash_si *h) {
	assert(h != NULL);

	return h->used;
}
/* }}} */
/* {{{ hash_si_capacity */
/** Returns the capacity of the hash map h */
size_t hash_si_capacity(struct hash_si *h) {
	assert(h != NULL);

	return h->mask + 1;
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
