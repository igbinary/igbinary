/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Author: Oleg Grenrus <oleg.grenrus@dynamoid.com>                     |
  | See CREDITS for contributors                                         |
  +----------------------------------------------------------------------+
*/

#ifndef HASH_H
#define HASH_H

#include <assert.h>

#ifdef PHP_WIN32
# include "ig_win32.h"
#else
# include <stdint.h>     /* defines uint32_t etc */
#endif

#include <stddef.h>
#include "zend_types.h"

/** Key/value pair of hash_si.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 * @see hash_si
 */
struct hash_si_pair {
	zend_string *key_zstr; /* Contains key, key length, and key hash */
	uint32_t key_hash;		/**< Copy of ZSTR_H(key_zstr) (or 1 if hash is truncated to 0). Avoid dereferencing key_zstr if hashes are different. */
	uint32_t value;		    /**< Value. */
};

enum hash_si_code {
	hash_si_code_inserted,
	hash_si_code_exists,
	hash_si_code_exception
};

struct hash_si_result {
	enum hash_si_code code;
	uint32_t value;
};

/** Hash-array.
 * Like c++ unordered_map<char *, int32_t>.
 * Current implementation uses linear probing (with interval 1, 3, 5, or 7).
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 */
struct hash_si {
	size_t mask; 					/**< Bitmask for the array. size == mask+1 */
	size_t used;					/**< Used size of array. */
	struct hash_si_pair *data;		/**< Pointer to array or pairs of data. */
};

/** Inits hash_si structure.
 * @param h pointer to hash_si struct.
 * @param size initial size of the hash array.
 * @return 0 on success, 1 else.
 */
int hash_si_init(struct hash_si *h, uint32_t size);

/** Frees hash_si structure.
 * Doesn't call free(h).
 * @param h pointer to hash_si struct.
 */
void hash_si_deinit(struct hash_si *h);

/** Inserts value into hash_si.
 * @param h Pointer to hash_si struct.
 * @param key Pointer to key.
 * @param key_len Key length.
 * @param value Value.
 * @return 0 on success, 1 or 2 else.
 */
/*
int hash_si_insert (struct hash_si *h, const char *key, size_t key_len, uint32_t value);
*/

/** Finds value from hash_si.
 * Value returned through value param.
 * @param h Pointer to hash_si struct.
 * @param key Pointer to key.
 * @param key_len Key length.
 * @param[out] value Found value.
 * @return 0 if found, 1 if not.
 */
/*
int hash_si_find (struct hash_si *h, const char *key, size_t key_len, uint32_t * value);
*/

/** Finds value from hash_si.
 * Value returned through value param.
 * @param h Pointer to hash_si struct.
 * @param key zend_string with key
 * @param[out] value Found value.
 * @return 0 if found, 1 if not.
 */
struct hash_si_result hash_si_find_or_insert(struct hash_si *h, zend_string *key, uint32_t value);

/** Remove value from hash_si.
 * Removed value is available through value param.
 * @param h Pointer to hash_si struct.
 * @param key Pointer to key.
 * @param key_len Key length.
 * @param[out] value Removed value.
 * @return 0 ivalue removed, 1 if not existed.
 */
/*
int hash_si_remove (struct hash_si *h, const char *key, size_t key_len, uint32_t * value);
*/

/** Travarses hash_si.
 * Calls traverse_function on every item. Traverse function should not modify hash
 * @param h Pointer to hash_si struct.
 * @param traverse_function Function to call on every item of hash_si.
 */
/*
void hash_si_traverse (struct hash_si *h, int (*traverse_function) (const char *key, size_t key_len, uint32_t value));
*/

/** Returns size of hash_si.
 * @param h Pointer to hash_si struct.
 * @return Size of hash_si.
 */
static zend_always_inline size_t hash_si_size(struct hash_si *h) {
	return h->used;
}

/** Returns capacity of hash_si.
 * @param h Pointer to hash_si struct.
 * @return Capacity of hash_si.
 */
static zend_always_inline size_t hash_si_capacity(struct hash_si *h) {
	return h->mask + 1;
}

#endif /* HASH_H */
