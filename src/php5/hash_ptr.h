/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Author: Oleg Grenrus <oleg.grenrus@dynamoid.com>                     |
  | See CREDITS for contributors                                         |
  | This defines hash_si_ptr.                                            |
  | It is like hash_si, but the key is always a non-zero zend_uintptr_t  |
  +----------------------------------------------------------------------+
*/

#ifndef HASH_PTR_H
#define HASH_PTR_H

#include <assert.h>

#ifdef PHP_WIN32
# include "ig_win32.h"
#else
# include <stdint.h>     /* defines uint32_t etc */
#endif

#include <stddef.h>
#include "zend_types.h"

// NULL converted to an integer, on sane platforms.
#define HASH_PTR_KEY_INVALID 0

/** Key/value pair of hash_si_ptr.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 * @see hash_si_ptr
 */
struct hash_si_ptr_pair
{
	zend_uintptr_t key; /**< The key: The address of a pointer, casted to an int (won't be dereferenced). */
	uint32_t value;		/**< Value. */
};

/** Hash-array.
 * Like c++ std::unordered_map<zend_uintptr_t, int32_t>, but does not allow HASH_PTR_KEY_INVALID as a key.
 * Current implementation uses linear probing.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 */
struct hash_si_ptr {
	size_t size; 					/**< Allocated size of array. */
	size_t used;					/**< Used size of array. */
	struct hash_si_ptr_pair *data;		/**< Pointer to array or pairs of data. */
};

/** Inits hash_si_ptr structure.
 * @param h pointer to hash_si_ptr struct.
 * @param size initial size of the hash array.
 * @return 0 on success, 1 else.
 */
int hash_si_ptr_init(struct hash_si_ptr *h, size_t size);

/** Frees hash_si_ptr structure.
 * Doesn't call free(h).
 * @param h pointer to hash_si_ptr struct.
 */
void hash_si_ptr_deinit(struct hash_si_ptr *h);

/** Inserts value into hash_si_ptr.
 * @param h Pointer to hash_si_ptr struct.
 * @param key Pointer to key.
 * @param key_len Key length.
 * @param value Value.
 * @return 0 on success, 1 or 2 else.
 */
/*
int hash_si_ptr_insert (struct hash_si_ptr *h, const zend_uintptr_t key, uint32_t value);
*/

/** Finds value from hash_si_ptr.
 * Value returned thru value param.
 * @param h Pointer to hash_si_ptr struct.
 * @param key Pointer to key.
 * @param key_len Key length.
 * @param[out] value Found value.
 * @return 0 if found, 1 if not.
 */
/*
int hash_si_ptr_find (struct hash_si_ptr *h, const zend_uintptr_t key, uint32_t * value);
*/

/** Returns size of hash_si_ptr.
 * @param h Pointer to hash_si_ptr struct.
 * @return Size of hash_si_ptr.
 */
size_t hash_si_ptr_size (struct hash_si_ptr *h);

/** 
 * If the key does not exist, add a mapping from key to value and returns SIZE_MAX
 * If the key does exist, return the corresponding value
 * @param h Pointer to hash_si_ptr struct.
 * @param key key to look up or add
 * @param value value to insert if it doesn't already exist
 *
 * @return SIZE_MAX or old, unmodified key
 */
size_t hash_si_ptr_find_or_insert(struct hash_si_ptr *h, const zend_uintptr_t key, uint32_t value);

/** Returns capacity of hash_si_ptr.
 * @param h Pointer to hash_si_ptr struct.
 * @return Capacity of hash_si_ptr.
 */
size_t hash_si_ptr_capacity (struct hash_si_ptr *h);

#endif /* HASH_PTR_H */
