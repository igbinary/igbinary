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

/** Key/value pair of hash_si.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 * @see hash_si
 */
struct hash_si_pair
{
	char *key;			/**< Pointer to key. */
	size_t key_len;		/**< Key length. */
	uint32_t key_hash;  /**< Key hash. */
	uint32_t value;		/**< Value. */
};

enum hash_si_code {
	hash_si_code_inserted,
	hash_si_code_exists,
	hash_si_code_exception
};

struct hash_si_result
{
	enum hash_si_code code;
	uint32_t value;
};

/** Hash-array.
 * Like c++ map<char *, int32_t>.
 * Current implementation uses linear probing.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 */
struct hash_si {
	size_t size; 					/**< Allocated size of array. */
	size_t used;					/**< Used size of array. */
	struct hash_si_pair *data;		/**< Pointer to array or pairs of data. */
};

/** Inits hash_si structure.
 * @param h pointer to hash_si struct.
 * @param size initial size of the hash array.
 * @return 0 on success, 1 else.
 */
int hash_si_init (struct hash_si *h, size_t size);

/** Frees hash_si structure.
 * Doesn't call free(h).
 * @param h pointer to hash_si struct.
 */
void hash_si_deinit (struct hash_si *h);

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
 * Value returned thru value param.
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
 * Value returned thru value param.
 * @param h Pointer to hash_si struct.
 * @param key Pointer to key.
 * @param key_len Key length.
 * @param[out] value Found value.
 * @return 0 if found, 1 if not.
 */
struct hash_si_result hash_si_find_or_insert(struct hash_si *h, const char *key, size_t key_len, uint32_t value);

/** Remove value from hash_si.
 * Removed value is available thru value param.
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
size_t hash_si_size (struct hash_si *h);

/** Returns capacity of hash_si.
 * @param h Pointer to hash_si struct.
 * @return Capacity of hash_si.
 */
size_t hash_si_capacity (struct hash_si *h);

#endif /* HASH_H */
