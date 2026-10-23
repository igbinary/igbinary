#ifndef IGBINARY_ZEND_HASH
#define IGBINARY_ZEND_HASH
/*
   +----------------------------------------------------------------------+
   | igbinary optimizations for find_or_insert adapted from Zend Engine.  |
   | Adaptations written by Tyson Andre for igbinary.                     |
   | original license of php-src/Zend/zend_hash.c is below.               |
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2018 Zend Technologies Ltd. (http://www.zend.com) |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Authors: Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   |          Dmitry Stogov <dmitry@zend.com>                             |
   +----------------------------------------------------------------------+
*/

/* See https://nikic.github.io/2014/12/22/PHPs-new-hashtable-implementation.html for an overview of PHP's hash table information */

#if PHP_VERSION_ID >= 80100 && !IGBINARY_FORCE_REFERENCE_HASH_TABLE_FUNCTIONS
/* PHP 8.1+ exports zend_hash_lookup()/zend_hash_index_lookup(), which have exactly the semantics needed here */
/* (return the existing value, or insert and return a new IS_NULL entry) using a single hash table probe. */
/* {{{ igbinary_zend_hash_add_or_find(HashTable *ht, zend_string *key) */
static zend_always_inline zval *igbinary_zend_hash_add_or_find(HashTable *ht, zend_string *key) {
	return zend_hash_lookup(ht, key);
}
/* }}} */
/* {{{ igbinary_zend_hash_index_add_or_find(HashTable *ht, zend_long key) */
static zend_always_inline zval *igbinary_zend_hash_index_add_or_find(HashTable *ht, zend_long key) {
	return zend_hash_index_lookup(ht, key);
}
/* }}} */
#elif PHP_VERSION_ID < 70200 || PHP_VERSION_ID >= 80100 || IGBINARY_FORCE_REFERENCE_HASH_TABLE_FUNCTIONS
/* {{{ igbinary_zend_hash_add_or_find(HashTable *ht, zend_string *key) unoptimized reference implementation. */
/* This will either return the pointer to the existing value, or add a brand new entry to the hash table with a PHP IS_NULL value. */
static zend_always_inline zval *igbinary_zend_hash_add_or_find(HashTable *ht, zend_string *key) {
	zval *vp;
	zval val;
	if (UNEXPECTED((vp = zend_hash_find(ht, key)) != NULL)) {
		return vp;
	}
	ZVAL_NULL(&val);
	return zend_hash_add_new(ht, key, &val);
}
/* }}} */
/* {{{ igbinary_zend_hash_add_or_find(HashTable *ht, zend_long key) unoptimized reference implementation. */
/* This will either return the pointer to the existing value, or add a brand new entry to the hash table with a PHP IS_NULL value. */
static zend_always_inline zval *igbinary_zend_hash_index_add_or_find(HashTable *ht, zend_long key) {
	zval *vp;
	zval val;
	if (UNEXPECTED((vp = zend_hash_index_find(ht, key)) != NULL)) {
		return vp;
	}
	ZVAL_NULL(&val);
	return zend_hash_index_add_new(ht, key, &val);
}
/* }}} */

/* This is either too old to bother implementing specializations for, or too new for the HashTable implementation to be finalized and tested */

#else
/* PHP 7.2 to 8.0 */

#if PHP_VERSION_ID < 70300
# define IS_ARRAY_PERSISTENT HASH_FLAG_PERSISTENT
# define zend_hash_real_init_mixed(ht) zend_hash_real_init((ht), 0)
# define zend_hash_real_init_packed(ht) zend_hash_real_init((ht), 1)
# define HT_SIZE_TO_MASK(size) (-(size))
# define HT_FLAGS(ht) ((ht)->u.flags)
static zend_always_inline zend_bool zend_string_equal_content(zend_string *s1, zend_string *s2)
{
	return ZSTR_LEN(s1) == ZSTR_LEN(s2) && !memcmp(ZSTR_VAL(s1), ZSTR_VAL(s2), ZSTR_LEN(s1));
}
#endif /* PHP_VERSION_ID < 70300 */

/* {{{ igbinary_zend_hash_find_bucket(const HashTable *ht, zend_string *key, zend_bool known_hash) */
static zend_always_inline Bucket *igbinary_zend_hash_find_bucket(const HashTable *ht, zend_string *key, zend_bool known_hash)
{
	zend_ulong h;
	uint32_t nIndex;
	uint32_t idx;
	Bucket *p, *arData;

	if (known_hash) {
		h = ZSTR_H(key);
	} else {
		h = zend_string_hash_val(key);
	}
	arData = ht->arData;
	nIndex = h | ht->nTableMask;
	idx = HT_HASH_EX(arData, nIndex);

	if (UNEXPECTED(idx == HT_INVALID_IDX)) {
		return NULL;
	}
	p = HT_HASH_TO_BUCKET_EX(arData, idx);
	if (p->key == key) { /* check for the same interned string */
		return p;
	}

	while (1) {
		if (p->h == ZSTR_H(key) &&
		    EXPECTED(p->key) &&
		    zend_string_equal_content(p->key, key)) {
			return p;
		}
		idx = Z_NEXT(p->val);
		if (idx == HT_INVALID_IDX) {
			return NULL;
		}
		p = HT_HASH_TO_BUCKET_EX(arData, idx);
		if (p->key == key) { /* check for the same interned string */
			return p;
		}
	}
}
/* }}} */
/* {{{ void igbinary_zend_hash_do_resize(HashTable *ht) */
static void ZEND_FASTCALL igbinary_zend_hash_do_resize(HashTable *ht)
{

	// IS_CONSISTENT(ht);
	// HT_ASSERT_RC1(ht);

	if (ht->nNumUsed > ht->nNumOfElements + (ht->nNumOfElements >> 5)) { /* additional term is there to amortize the cost of compaction */
		zend_hash_rehash(ht);
	} else if (ht->nTableSize < HT_MAX_SIZE) {	/* Let's double the table size */
		void *new_data, *old_data = HT_GET_DATA_ADDR(ht);
		uint32_t nSize = ht->nTableSize + ht->nTableSize;
		Bucket *old_buckets = ht->arData;

		ht->nTableSize = nSize;
#if PHP_VERSION_ID >= 70300
		new_data = pemalloc(HT_SIZE_EX(nSize, HT_SIZE_TO_MASK(nSize)), GC_FLAGS(ht) & IS_ARRAY_PERSISTENT);
#else
		new_data = pemalloc(HT_SIZE(ht), GC_FLAGS(ht) & IS_ARRAY_PERSISTENT);
#endif
		ht->nTableMask = HT_SIZE_TO_MASK(ht->nTableSize);
		HT_SET_DATA_ADDR(ht, new_data);
		memcpy(ht->arData, old_buckets, sizeof(Bucket) * ht->nNumUsed);
		pefree(old_data, GC_FLAGS(ht) & IS_ARRAY_PERSISTENT);
		zend_hash_rehash(ht);
	} else {
		zend_error_noreturn(E_ERROR, "Possible integer overflow in memory allocation (%u * %zu + %zu)", ht->nTableSize * 2, sizeof(Bucket) + sizeof(uint32_t), sizeof(Bucket));
	}
}
/* }}} */
static void ZEND_FASTCALL zend_hash_packed_grow(HashTable *ht) /* {{{ */
{
	// HT_ASSERT_RC1(ht);
	if (ht->nTableSize >= HT_MAX_SIZE) {
		zend_error_noreturn(E_ERROR, "Possible integer overflow in memory allocation (%u * %zu + %zu)", ht->nTableSize * 2, sizeof(Bucket), sizeof(Bucket));
	}
	ht->nTableSize += ht->nTableSize;
	HT_SET_DATA_ADDR(ht, perealloc2(HT_GET_DATA_ADDR(ht), HT_SIZE_EX(ht->nTableSize, HT_MIN_MASK), HT_USED_SIZE(ht), GC_FLAGS(ht) & IS_ARRAY_PERSISTENT));
}
/* }}} */
static zend_always_inline Bucket *zend_hash_index_find_bucket(const HashTable *ht, zend_ulong h) /* {{{ */
{
	uint32_t nIndex;
	uint32_t idx;
	Bucket *p, *arData;

	arData = ht->arData;
	nIndex = h | ht->nTableMask;
	idx = HT_HASH_EX(arData, nIndex);
	while (idx != HT_INVALID_IDX) {
		ZEND_ASSERT(idx < HT_IDX_TO_HASH(ht->nTableSize));
		p = HT_HASH_TO_BUCKET_EX(arData, idx);
		if (p->h == h && !p->key) {
			return p;
		}
		idx = Z_NEXT(p->val);
	}
	return NULL;
} /* }}} */

/* Methods used by igbinary.c */
/* {{{ zval *igbinary_zend_hash_add_or_find(HashTable *ht, zend_string *key) */
/* Source: zend_hash_add_or_update_i with adaptions */
static zend_always_inline zval *igbinary_zend_hash_add_or_find(HashTable *ht, zend_string *key)
{
	zend_ulong h;
	uint32_t nIndex;
	uint32_t idx;
	Bucket *p, *arData;

	// IS_CONSISTENT(ht);
	// HT_ASSERT_RC1(ht);

#if PHP_VERSION_ID >= 70400
	if (UNEXPECTED(HT_FLAGS(ht) & (HASH_FLAG_UNINITIALIZED|HASH_FLAG_PACKED)))
#else
	if (UNEXPECTED(((HT_FLAGS(ht) ^ HASH_FLAG_INITIALIZED) & (HASH_FLAG_INITIALIZED|HASH_FLAG_PACKED))))
#endif
	{
#if PHP_VERSION_ID >= 70400
		if (EXPECTED(HT_FLAGS(ht) & HASH_FLAG_UNINITIALIZED))
#else
		if (EXPECTED(!(HT_FLAGS(ht) & HASH_FLAG_INITIALIZED)))
#endif
		{
			zend_hash_real_init_mixed(ht);
			if (!ZSTR_IS_INTERNED(key)) {
				zend_string_addref(key);
				HT_FLAGS(ht) &= ~HASH_FLAG_STATIC_KEYS;
				zend_string_hash_val(key);
			}
			goto add_to_hash;
		} else {
			zend_hash_packed_to_hash(ht);
			if (!ZSTR_IS_INTERNED(key)) {
				zend_string_addref(key);
				HT_FLAGS(ht) &= ~HASH_FLAG_STATIC_KEYS;
				zend_string_hash_val(key);
			}
		}
	} else {
		p = igbinary_zend_hash_find_bucket(ht, key, 0);

		if (p) {
			return &p->val;
		}
		if (!ZSTR_IS_INTERNED(key)) {
			zend_string_addref(key);
			HT_FLAGS(ht) &= ~HASH_FLAG_STATIC_KEYS;
		}
	}

	/* If the Hash table is full, resize it */
	if ((ht)->nNumUsed >= (ht)->nTableSize) {
		igbinary_zend_hash_do_resize(ht);
	}

add_to_hash:
	idx = ht->nNumUsed++;
	ht->nNumOfElements++;
#if PHP_VERSION_ID < 70300
	if (ht->nInternalPointer == HT_INVALID_IDX) {
		ht->nInternalPointer = idx;
	}
	zend_hash_iterators_update(ht, HT_INVALID_IDX, idx);
#endif
	arData = ht->arData;
	p = arData + idx;
	p->key = key;
	p->h = h = ZSTR_H(key);
	nIndex = h | ht->nTableMask;
	Z_NEXT(p->val) = HT_HASH_EX(arData, nIndex);
	HT_HASH_EX(arData, nIndex) = HT_IDX_TO_HASH(idx);
	ZVAL_NULL(&p->val);

	return &p->val;
}
/* }}} */
static zend_always_inline zval *igbinary_zend_hash_index_add_or_find(HashTable *ht, zend_ulong h) /* {{{ */
{
	uint32_t nIndex;
	uint32_t idx;
	Bucket *p;

	// IS_CONSISTENT(ht);
	// HT_ASSERT_RC1(ht);

	if (HT_FLAGS(ht) & HASH_FLAG_PACKED) {
		if (h < ht->nNumUsed) {
			p = ht->arData + h;
			if (Z_TYPE(p->val) != IS_UNDEF) {
replace:
				return &p->val;
			} else { /* we have to keep the order :( */
				goto convert_to_hash;
			}
		} else if (EXPECTED(h < ht->nTableSize)) {
add_to_packed:
			p = ht->arData + h;
			/* incremental initialization of empty Buckets */
			if (h > ht->nNumUsed) {
				Bucket *q = ht->arData + ht->nNumUsed;
				while (q != p) {
					ZVAL_UNDEF(&q->val);
					q++;
				}
			}
			ht->nNextFreeElement = ht->nNumUsed = h + 1;
			goto add;
		} else if ((h >> 1) < ht->nTableSize &&
		           (ht->nTableSize >> 1) < ht->nNumOfElements) {
			zend_hash_packed_grow(ht);
			goto add_to_packed;
		} else {
			if (ht->nNumUsed >= ht->nTableSize) {
				ht->nTableSize += ht->nTableSize;
			}
convert_to_hash:
			zend_hash_packed_to_hash(ht);
		}
#if PHP_VERSION_ID >= 70400
	} else if (HT_FLAGS(ht) & HASH_FLAG_UNINITIALIZED) {
#else
	} else if (!(HT_FLAGS(ht) & HASH_FLAG_INITIALIZED)) {
#endif
		if (h < ht->nTableSize) {
			zend_hash_real_init_packed(ht);
			goto add_to_packed;
		}
		zend_hash_real_init_mixed(ht);
	} else {
		p = zend_hash_index_find_bucket(ht, h);
		if (p) {
			goto replace;
		}
		/* If the Hash table is full, resize it */
		if ((ht)->nNumUsed >= (ht)->nTableSize) {
			igbinary_zend_hash_do_resize(ht);
		}
	}

	idx = ht->nNumUsed++;
	nIndex = h | ht->nTableMask;
	p = ht->arData + idx;
	Z_NEXT(p->val) = HT_HASH(ht, nIndex);
	HT_HASH(ht, nIndex) = HT_IDX_TO_HASH(idx);
	if ((zend_long)h >= ht->nNextFreeElement) {
		ht->nNextFreeElement = (zend_long)h < ZEND_LONG_MAX ? h + 1 : ZEND_LONG_MAX;
	}
add:
	ht->nNumOfElements++;
#if PHP_VERSION_ID < 70300
	if (ht->nInternalPointer == HT_INVALID_IDX) {
		ht->nInternalPointer = ht->nNumUsed - 1;
	}
	zend_hash_iterators_update(ht, HT_INVALID_IDX, ht->nNumUsed - 1);
#endif
	p->h = h;
	p->key = NULL;
	ZVAL_NULL(&p->val);

	return &p->val;
}
/* }}} */

#endif /* PHP 7.2 to 8.0 */
/*
 * Local variables:
 * tab-width: 2
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
#endif /* IGBINARY_ZEND_HASH */
