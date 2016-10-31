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

#include "igbinary_macros.h"

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
/* {{{ hash_si_init */
int hash_si_init(struct hash_si *h, size_t size) {
	size = nextpow2(size);

	h->size = size;
	h->used = 0;
	h->data = (struct hash_si_pair *) emalloc(sizeof(struct hash_si_pair) * size);
	if (h->data == NULL) {
		return 1;
	}

	memset(h->data, 0, sizeof(struct hash_si_pair) * size);

	return 0;
}
/* }}} */
/* {{{ hash_si_deinit */
void hash_si_deinit(struct hash_si *h) {
	size_t i;

	for (i = 0; i < h->size; i++) {
		if (h->data[i].key != NULL) {
			efree(h->data[i].key);
		}
	}

	efree(h->data);

	h->size = 0;
	h->used = 0;
}
/* }}} */
/* {{{ _hash_si_find */
/** Returns index of key, or where it should be.
 * @param h Pointer to hash_si struct.
 * @param key Pointer to key.
 * @param key_len Key length.
 * @param key_hash precomputed key hash of bytes of key (=zend_inline_hash_func(key, key_len))
 * @return index.
 */
inline static size_t _hash_si_find(const struct hash_si *h, const char *key, const size_t key_len, const uint32_t key_hash) {
	uint32_t hv;
	size_t size;
	size_t mask;

	assert(h != NULL);

	size = h->size;
	mask = size - 1;
	hv = key_hash & mask;

	while (size > 0 &&
		h->data[hv].key != NULL &&
		(h->data[hv].key_hash != key_hash || h->data[hv].key_len != key_len || UNEXPECTED(memcmp(h->data[hv].key, key, key_len) != 0))) {
		/* linear prob */
		hv = (hv + 1) & mask;
		size--;
	}

	return hv;
}
/* }}} */
/* {{{ hash_si_rehash */
/** Rehash/resize hash_si.
 * @param h Pointer to hash_si struct.
 */
inline static void hash_si_rehash(struct hash_si *h) {
	size_t i;
	struct hash_si newh;

	assert(h != NULL);

	hash_si_init(&newh, h->size * 2);

	for (i = 0; i < h->size; i++) {
		const struct hash_si_pair *old_pair = &(h->data[i]);
		if (old_pair->key != NULL) {
			uint32_t hv = _hash_si_find(&newh, old_pair->key, old_pair->key_len, old_pair->key_hash);
			newh.data[hv] = *old_pair;
		}
	}

	efree(h->data);
	h->data = newh.data;
	h->size *= 2;
}
/* }}} */
/* {{{ hash_si_insert */
/*
int hash_si_insert(struct hash_si *h, const char *key, size_t key_len, uint32_t value) {
	uint32_t hv;

	if (h->size / 4 * 3 < h->used + 1) {
		hash_si_rehash(h);
	}

	hv = _hash_si_find(h, key, key_len);

	if (h->data[hv].key == NULL) {
		h->data[hv].key = (char *) emalloc(key_len + 1);
		if (h->data[hv].key == NULL) {
			return 1;
		}
		memcpy(h->data[hv].key, key, key_len);
		h->data[hv].key[key_len] = '\0';
		h->data[hv].key_len = key_len;

		h->used++;
	} else {
		return 2;
	}

	h->data[hv].value = value;

	return 0;
}
*/
/* }}} */
/* {{{ hash_si_find */
/*
int hash_si_find(struct hash_si *h, const char *key, size_t key_len, uint32_t *value) {
	uint32_t hv;

	assert(h != NULL);

	hv = _hash_si_find(h, key, key_len);

	if (h->data[hv].key == NULL) {
		return 1;
	} else {
		*value = h->data[hv].value;
		return 0;
	}
}
*/
/* }}} */
/* {{{ hash_si_find_or_insert */
struct hash_si_result hash_si_find_or_insert(struct hash_si *h, const char *key, size_t key_len, uint32_t value) {
	uint32_t hv;
	uint32_t key_hash;
	struct hash_si_result result;
	struct hash_si_pair *pair;

	assert(h != NULL);

	key_hash = zend_inline_hash_func(key, key_len);
	hv = _hash_si_find(h, key, key_len, key_hash);
	pair = &h->data[hv];

	if (pair->key == NULL) {
		char* copy = emalloc(key_len);
		if (copy == NULL) {
			result.code = hash_si_code_exception;
			return result;
		}
		memcpy(copy, key, key_len);

		pair->key = copy;
		pair->key_len = key_len;
		pair->key_hash = key_hash;
		pair->value = value;

		h->used++;
		if (h->size / 4 * 3 < h->used) {
			hash_si_rehash(h);
		}

		result.code = hash_si_code_inserted;
		return result;
	} else {
		result.code = hash_si_code_exists;
		result.value = h->data[hv].value;
		return result;
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
size_t hash_si_size(struct hash_si *h) {
	assert(h != NULL);

	return h->used;
}
/* }}} */
/* {{{ hash_si_capacity */
size_t hash_si_capacity(struct hash_si *h) {
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
