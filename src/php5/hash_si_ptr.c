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

#ifdef PHP_WIN32
# include "win32/php_stdint.h"
#else
# include <stdint.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "hash_ptr.h"
#include "zend.h"

/* Function similar to zend_inline_hash_func. This is not identical. */
inline static uint32_t inline_hash_of_address(zend_uintptr_t ptr) {
	register uint32_t hash = 5381;
	/* Note: Hash the least significant bytes first - Those need to influence the final result as much as possible. */
	hash = ((hash << 5) + hash) + (ptr & 0xff);
	hash = ((hash << 5) + hash) + ((ptr >> 8) & 0xff);
	hash = ((hash << 5) + hash) + ((ptr >> 16) & 0xff);
	hash = ((hash << 5) + hash) + ((ptr >> 24) & 0xff);
#if UINTPTR_MAX > UINT32_MAX
	hash = ((hash << 5) + hash) + ((ptr >> 32) & 0xff);
	hash = ((hash << 5) + hash) + ((ptr >> 40) & 0xff);
	hash = ((hash << 5) + hash) + ((ptr >> 48) & 0xff);
	hash = ((hash << 5) + hash) + ((ptr >> 56) & 0xff);
#endif
	return hash;
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
int hash_si_ptr_init(struct hash_si_ptr *h, size_t size) {
	size = nextpow2(size);

	h->size = size;
	h->used = 0;
	h->data = (struct hash_si_ptr_pair*) malloc(sizeof(struct hash_si_ptr_pair) * size);
	if (h->data == NULL) {
		return 1;
	}

	memset(h->data, 0, sizeof(struct hash_si_ptr_pair) * size); /* Set everything to 0. sets keys to HASH_PTR_KEY_INVALID. */

	return 0;
}
/* }}} */
/* {{{ hash_si_ptr_deinit */
void hash_si_ptr_deinit(struct hash_si_ptr *h) {
	size_t i;

	free(h->data);
	h->data = NULL;

	h->size = 0;
	h->used = 0;
}
/* }}} */
/* {{{ _hash_si_ptr_find */
/** Returns index of key, or where it should be.
 * @param h Pointer to hash_si_ptr struct.
 * @param key Pointer to key.
 * @return index.
 */
inline static size_t _hash_si_ptr_find(struct hash_si_ptr *h, const zend_uintptr_t key) {
	uint32_t hv;
	size_t size;

	assert(h != NULL);

	size = h->size;
	hv = inline_hash_of_address(key) & (h->size-1);

	while (size > 0 &&
		h->data[hv].key != HASH_PTR_KEY_INVALID &&
		h->data[hv].key != key) {
		/* linear prob */
		hv = (hv + 1) & (h->size-1);
		size--;
	}

	return hv;
}
/* }}} */
/* }}} */
/* {{{ hash_si_ptr_rehash */
/** Rehash/resize hash_si_ptr.
 * @param h Pointer to hash_si_ptr struct.
 */
inline static void hash_si_ptr_rehash(struct hash_si_ptr *h) {
	uint32_t hv;
	size_t i;
	struct hash_si_ptr newh;

	assert(h != NULL);

	hash_si_ptr_init(&newh, h->size * 2);

	for (i = 0; i < h->size; i++) {
		if (h->data[i].key != HASH_PTR_KEY_INVALID) {
			hv = _hash_si_ptr_find(&newh, h->data[i].key);
			newh.data[hv].key = h->data[i].key;
			newh.data[hv].value = h->data[i].value;
		}
	}

	free(h->data);
	h->data = newh.data;
	h->size *= 2;
}
/* }}} */
/* {{{ hash_si_ptr_insert */
/*
int hash_si_ptr_insert(struct hash_si_ptr *h, const zend_uintptr_t key, uint32_t value) {
	uint32_t hv;

	if (h->size / 4 * 3 < h->used + 1) {
		hash_si_ptr_rehash(h);
	}

	hv = _hash_si_ptr_find(h, key);

	if (h->data[hv].key == HASH_PTR_KEY_INVALID) {
		h->data[hv].key = key;

		h->used++;
	} else {
		return 2;
	}

	h->data[hv].value = value;

	return 0;
}
*/
/* }}} */
/* {{{ hash_si_ptr_find */
/*
int hash_si_ptr_find(struct hash_si_ptr *h, const zend_uintptr_t key, uint32_t *value) {
	uint32_t hv;

	assert(h != NULL);

	hv = _hash_si_ptr_find(h, key);

	if (h->data[hv].key == HASH_PTR_KEY_INVALID) {
		return 1;
	} else {
		*value = h->data[hv].value;
		return 0;
	}
}
*/
/* }}} */
/* {{{ hash_si_ptr_find_or_insert */
size_t hash_si_ptr_find_or_insert(struct hash_si_ptr *h, const zend_uintptr_t key, uint32_t value) {
	uint32_t hv;

	hv = _hash_si_ptr_find(h, key);

	if (h->data[hv].key == HASH_PTR_KEY_INVALID) {
		h->data[hv].key = key;
		h->data[hv].value = value;
		h->used++;

		/* The size increased, so check if we need to expand the map */
		if (h->size / 4 * 3 < h->used) {
			hash_si_ptr_rehash(h);
		}
		return SIZE_MAX;
	} else {
		return h->data[hv].value;
	}
}
/* }}} */
/* {{{ hash_si_ptr_size */
size_t hash_si_ptr_size(struct hash_si_ptr *h) {
	assert(h != NULL);

	return h->used;
}
/* }}} */
/* {{{ hash_si_ptr_capacity */
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
