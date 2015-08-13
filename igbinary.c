/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Author: Oleg Grenrus <oleg.grenrus@dynamoid.com>                     |
  | See CREDITS for contributors                                         |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef PHP_WIN32
# include "ig_win32.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "zend_alloc.h"
#include "ext/standard/info.h"
#include "ext/standard/php_var.h"

#if HAVE_PHP_SESSION
# include "ext/session/php_session.h"
#endif /* HAVE_PHP_SESSION */

#include "ext/standard/php_incomplete_class.h"

#if defined(HAVE_APCU_SUPPORT)
# include "ext/apcu/apc_serializer.h"
#elif defined(HAVE_APC_SUPPORT)
# if USE_BUNDLED_APC
#  include "apc_serializer.h"
# else
#  include "ext/apc/apc_serializer.h"
# endif
#endif /* HAVE_APCU_SUPPORT || HAVE_APC_SUPPORT */

#include "php_igbinary.h"

#include "igbinary.h"

#include <assert.h>

#ifndef PHP_WIN32
# include <inttypes.h>
# include <stdbool.h>
# include <stdint.h>
#endif


#include <stddef.h>
#include "hash.h"

#if HAVE_PHP_SESSION
/** Session serializer function prototypes. */
PS_SERIALIZER_FUNCS(igbinary);
#endif /* HAVE_PHP_SESSION */

#if defined(HAVE_APC_SUPPORT) || defined(HAVE_APCU_SUPPORT)
/** Apc serializer function prototypes */
static int APC_SERIALIZER_NAME(igbinary) (APC_SERIALIZER_ARGS);
static int APC_UNSERIALIZER_NAME(igbinary) (APC_UNSERIALIZER_ARGS);
#endif

/* {{{ Types */
enum igbinary_type {
	/* 00 */ igbinary_type_null,			/**< Null. */

	/* 01 */ igbinary_type_ref8,			/**< Array reference. */
	/* 02 */ igbinary_type_ref16,			/**< Array reference. */
	/* 03 */ igbinary_type_ref32,			/**< Array reference. */

	/* 04 */ igbinary_type_bool_false,		/**< Boolean true. */
	/* 05 */ igbinary_type_bool_true,		/**< Boolean false. */

	/* 06 */ igbinary_type_long8p,			/**< Long 8bit positive. */
	/* 07 */ igbinary_type_long8n,			/**< Long 8bit negative. */
	/* 08 */ igbinary_type_long16p,			/**< Long 16bit positive. */
	/* 09 */ igbinary_type_long16n,			/**< Long 16bit negative. */
	/* 0a */ igbinary_type_long32p,			/**< Long 32bit positive. */
	/* 0b */ igbinary_type_long32n,			/**< Long 32bit negative. */

	/* 0c */ igbinary_type_double,			/**< Double. */

	/* 0d */ igbinary_type_string_empty,	/**< Empty string. */

	/* 0e */ igbinary_type_string_id8,		/**< String id. */
	/* 0f */ igbinary_type_string_id16,		/**< String id. */
	/* 10 */ igbinary_type_string_id32,		/**< String id. */

	/* 11 */ igbinary_type_string8,			/**< String. */
	/* 12 */ igbinary_type_string16,		/**< String. */
	/* 13 */ igbinary_type_string32,		/**< String. */

	/* 14 */ igbinary_type_array8,			/**< Array. */
	/* 15 */ igbinary_type_array16,			/**< Array. */
	/* 16 */ igbinary_type_array32,			/**< Array. */

	/* 17 */ igbinary_type_object8,			/**< Object. */
	/* 18 */ igbinary_type_object16,		/**< Object. */
	/* 19 */ igbinary_type_object32,		/**< Object. */

	/* 1a */ igbinary_type_object_id8,		/**< Object string id. */
	/* 1b */ igbinary_type_object_id16,		/**< Object string id. */
	/* 1c */ igbinary_type_object_id32,		/**< Object string id. */

	/* 1d */ igbinary_type_object_ser8,		/**< Object serialized data. */
	/* 1e */ igbinary_type_object_ser16,	/**< Object serialized data. */
	/* 1f */ igbinary_type_object_ser32,	/**< Object serialized data. */

	/* 20 */ igbinary_type_long64p,			/**< Long 64bit positive. */
	/* 21 */ igbinary_type_long64n,			/**< Long 64bit negative. */

	/* 22 */ igbinary_type_objref8,			/**< Object reference. */
	/* 23 */ igbinary_type_objref16,		/**< Object reference. */
	/* 24 */ igbinary_type_objref32,		/**< Object reference. */

	/* 25 */ igbinary_type_ref,				/**< Simple reference */
};

/** Serializer data.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 */
struct igbinary_serialize_data {
	uint8_t *buffer;			/**< Buffer. */
	size_t buffer_size;			/**< Buffer size. */
	size_t buffer_capacity;		/**< Buffer capacity. */
	bool scalar;				/**< Serializing scalar. */
	bool compact_strings;		/**< Check for duplicate strings. */
	struct hash_si strings;		/**< Hash of already serialized strings. */
	struct hash_si objects;		/**< Hash of already serialized objects. */
	int string_count;			/**< Serialized string count, used for back referencing */
	int error;					/**< Error number. Not used. */
	struct igbinary_memory_manager	mm; /**< Memory management functions. */
};

/** String/len pair for the igbinary_unserializer_data.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 * @see igbinary_unserialize_data.
 */
struct igbinary_unserialize_string_pair {
	char *data;		/**< Data. */
	size_t len;		/**< Data length. */
};

/** Unserializer data.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 */
struct igbinary_unserialize_data {
	uint8_t *buffer;				/**< Buffer. */
	size_t buffer_size;				/**< Buffer size. */
	size_t buffer_offset;			/**< Current read offset. */

	struct igbinary_unserialize_string_pair *strings; /**< Unserialized strings. */
	size_t strings_count;			/**< Unserialized string count. */
	size_t strings_capacity;		/**< Unserialized string array capacity. */

	void **references;				/**< Unserialized Arrays/Objects. */
	size_t references_count;		/**< Unserialized array/objects count. */
	size_t references_capacity;		/**< Unserialized array/object array capacity. */

	int error;						/**< Error number. Not used. */
	smart_string string0_buf;			/**< Temporary buffer for strings */
};
/* }}} */
/* {{{ Memory allocator wrapper prototypes */
static inline void *igbinary_mm_wrapper_malloc(size_t size, void *context);
static inline void *igbinary_mm_wrapper_realloc(void *ptr, size_t size, void *context);
static inline void igbinary_mm_wrapper_free(void *ptr, void *context);
/* }}} */
/* {{{ Serializing functions prototypes */
inline static int igbinary_serialize_data_init(struct igbinary_serialize_data *igsd, bool scalar, struct igbinary_memory_manager *memory_manager TSRMLS_DC);
inline static void igbinary_serialize_data_deinit(struct igbinary_serialize_data *igsd, int free_buffer TSRMLS_DC);

inline static int igbinary_serialize_header(struct igbinary_serialize_data *igsd TSRMLS_DC);

inline static int igbinary_serialize8(struct igbinary_serialize_data *igsd, uint8_t i TSRMLS_DC);
inline static int igbinary_serialize16(struct igbinary_serialize_data *igsd, uint16_t i TSRMLS_DC);
inline static int igbinary_serialize32(struct igbinary_serialize_data *igsd, uint32_t i TSRMLS_DC);
inline static int igbinary_serialize64(struct igbinary_serialize_data *igsd, uint64_t i TSRMLS_DC);

inline static int igbinary_serialize_null(struct igbinary_serialize_data *igsd TSRMLS_DC);
inline static int igbinary_serialize_bool(struct igbinary_serialize_data *igsd, int b TSRMLS_DC);
inline static int igbinary_serialize_long(struct igbinary_serialize_data *igsd, long l TSRMLS_DC);
inline static int igbinary_serialize_double(struct igbinary_serialize_data *igsd, double d TSRMLS_DC);
inline static int igbinary_serialize_string(struct igbinary_serialize_data *igsd, char *s, size_t len TSRMLS_DC);
inline static int igbinary_serialize_chararray(struct igbinary_serialize_data *igsd, const char *s, size_t len TSRMLS_DC);

inline static int igbinary_serialize_array(struct igbinary_serialize_data *igsd, zval *z, bool object, bool incomplete_class TSRMLS_DC);
inline static int igbinary_serialize_array_ref(struct igbinary_serialize_data *igsd, zval *z, bool object TSRMLS_DC);
inline static int igbinary_serialize_array_sleep(struct igbinary_serialize_data *igsd, zval *z, HashTable *ht, zend_class_entry *ce, bool incomplete_class TSRMLS_DC);
inline static int igbinary_serialize_object_name(struct igbinary_serialize_data *igsd, const char *name, size_t name_len TSRMLS_DC);
inline static int igbinary_serialize_object(struct igbinary_serialize_data *igsd, zval *z TSRMLS_DC);

static int igbinary_serialize_zval(struct igbinary_serialize_data *igsd, zval *z TSRMLS_DC);
/* }}} */
/* {{{ Unserializing functions prototypes */
inline static int igbinary_unserialize_data_init(struct igbinary_unserialize_data *igsd TSRMLS_DC);
inline static void igbinary_unserialize_data_deinit(struct igbinary_unserialize_data *igsd TSRMLS_DC);

inline static int igbinary_unserialize_header(struct igbinary_unserialize_data *igsd TSRMLS_DC);

inline static uint8_t igbinary_unserialize8(struct igbinary_unserialize_data *igsd TSRMLS_DC);
inline static uint16_t igbinary_unserialize16(struct igbinary_unserialize_data *igsd TSRMLS_DC);
inline static uint32_t igbinary_unserialize32(struct igbinary_unserialize_data *igsd TSRMLS_DC);
inline static uint64_t igbinary_unserialize64(struct igbinary_unserialize_data *igsd TSRMLS_DC);

inline static int igbinary_unserialize_long(struct igbinary_unserialize_data *igsd, enum igbinary_type t, long *ret TSRMLS_DC);
inline static int igbinary_unserialize_double(struct igbinary_unserialize_data *igsd, enum igbinary_type t, double *ret TSRMLS_DC);
inline static int igbinary_unserialize_string(struct igbinary_unserialize_data *igsd, enum igbinary_type t, char **s, size_t *len TSRMLS_DC);
inline static int igbinary_unserialize_chararray(struct igbinary_unserialize_data *igsd, enum igbinary_type t, char **s, size_t *len TSRMLS_DC);

inline static int igbinary_unserialize_array(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *z, int object TSRMLS_DC);
inline static int igbinary_unserialize_object(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *z TSRMLS_DC);
inline static int igbinary_unserialize_object_ser(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *z, zend_class_entry *ce TSRMLS_DC);
inline static int igbinary_unserialize_ref(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval **z TSRMLS_DC);

static int igbinary_unserialize_zval(struct igbinary_unserialize_data *igsd, zval *z TSRMLS_DC);
/* }}} */
/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_igbinary_serialize, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_igbinary_unserialize, 0, 0, 1)
	ZEND_ARG_INFO(0, str)
ZEND_END_ARG_INFO()
/* }}} */
/* {{{ igbinary_functions[] */
/** Exported php functions. */
zend_function_entry igbinary_functions[] = {
	PHP_FE(igbinary_serialize,                arginfo_igbinary_serialize)
	PHP_FE(igbinary_unserialize,              arginfo_igbinary_unserialize)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ igbinary dependencies */
#if ZEND_MODULE_API_NO >= 20050922
static const zend_module_dep igbinary_module_deps[] = {
	ZEND_MOD_REQUIRED("standard")
#ifdef HAVE_PHP_SESSION
	ZEND_MOD_REQUIRED("session")
#endif
#if defined(HAVE_APCU_SUPPORT)
	ZEND_MOD_OPTIONAL("apcu")
#elif defined(HAVE_APC_SUPPORT)
	ZEND_MOD_OPTIONAL("apc")
#endif
	{NULL, NULL, NULL}
};
#endif
/* }}} */

/* {{{ igbinary_module_entry */
zend_module_entry igbinary_module_entry = {
#if ZEND_MODULE_API_NO >= 20050922
	STANDARD_MODULE_HEADER_EX, NULL,
	igbinary_module_deps,
#elif ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"igbinary",
	igbinary_functions,
	PHP_MINIT(igbinary),
	PHP_MSHUTDOWN(igbinary),
	NULL,
	NULL,
	PHP_MINFO(igbinary),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_IGBINARY_VERSION, /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

ZEND_DECLARE_MODULE_GLOBALS(igbinary)

/* {{{ ZEND_GET_MODULE */
#ifdef COMPILE_DL_IGBINARY
ZEND_GET_MODULE(igbinary)
#endif
/* }}} */

/* {{{ INI entries */
PHP_INI_BEGIN()
	STD_PHP_INI_BOOLEAN("igbinary.compact_strings", "1", PHP_INI_ALL, OnUpdateBool, compact_strings, zend_igbinary_globals, igbinary_globals)
PHP_INI_END()
/* }}} */

/* {{{ php_igbinary_init_globals */
static void php_igbinary_init_globals(zend_igbinary_globals *igbinary_globals) {
	igbinary_globals->compact_strings = 1;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(igbinary) {
	(void) type;
	(void) module_number;
	ZEND_INIT_MODULE_GLOBALS(igbinary, php_igbinary_init_globals, NULL);

#if HAVE_PHP_SESSION
	php_session_register_serializer("igbinary",
		PS_SERIALIZER_ENCODE_NAME(igbinary),
		PS_SERIALIZER_DECODE_NAME(igbinary));
#endif

#if defined(HAVE_APC_SUPPORT) || defined(HAVE_APCU_SUPPORT)
	apc_register_serializer("igbinary",
		APC_SERIALIZER_NAME(igbinary),
		APC_UNSERIALIZER_NAME(igbinary),
		NULL TSRMLS_CC);
#endif

	REGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */
/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(igbinary) {
	(void) type;
	(void) module_number;

#ifdef ZTS
	ts_free_id(igbinary_globals_id);
#endif

	/*
	 * unregister serializer?
	 */
	UNREGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */
/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(igbinary) {
	(void) zend_module;
	php_info_print_table_start();
	php_info_print_table_row(2, "igbinary support", "enabled");
	php_info_print_table_row(2, "igbinary version", PHP_IGBINARY_VERSION);
#if defined(HAVE_APCU_SUPPORT)
	php_info_print_table_row(2, "igbinary APCU serializer ABI", APC_SERIALIZER_ABI);
#elif defined(HAVE_APC_SUPPORT)
	php_info_print_table_row(2, "igbinary APC serializer ABI", APC_SERIALIZER_ABI);
#else
	php_info_print_table_row(2, "igbinary APC serializer ABI", "no");
#endif
#if HAVE_PHP_SESSION
	php_info_print_table_row(2, "igbinary session support", "yes");
#else
	php_info_print_table_row(2, "igbinary session support", "no");
#endif
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */
/* {{{ Memory allocator wrappers */
static inline void *igbinary_mm_wrapper_malloc(size_t size, void *context)
{
    return emalloc(size);
}

static inline void *igbinary_mm_wrapper_realloc(void *ptr, size_t size, void *context)
{
    return erealloc(ptr, size);
}

static inline void igbinary_mm_wrapper_free(void *ptr, void *context)
{
    return efree(ptr);
}
/* }}} */
/* {{{ int igbinary_serialize(uint8_t**, size_t*, zval*) */
IGBINARY_API int igbinary_serialize(uint8_t **ret, size_t *ret_len, zval *z TSRMLS_DC) {
	return igbinary_serialize_ex(ret, ret_len, z, NULL TSRMLS_CC);
}
/* }}} */
/* {{{ int igbinary_serialize_ex(uint8_t**, size_t*, zval*, igbinary_memory_manager*) */
IGBINARY_API int igbinary_serialize_ex(uint8_t **ret, size_t *ret_len, zval *z, struct igbinary_memory_manager *memory_manager TSRMLS_DC) {
	struct igbinary_serialize_data igsd;
	uint8_t *tmpbuf;

	if (igbinary_serialize_data_init(&igsd, Z_TYPE_P(z) != IS_OBJECT && Z_TYPE_P(z) != IS_ARRAY, memory_manager TSRMLS_CC)) {
		zend_error(E_WARNING, "igbinary_serialize: cannot init igsd");
		return 1;
	}

	if (igbinary_serialize_header(&igsd TSRMLS_CC) != 0) {
		zend_error(E_WARNING, "igbinary_serialize: cannot write header");
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		return 1;
	}

	if (igbinary_serialize_zval(&igsd, z TSRMLS_CC) != 0) {
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		return 1;
	}

	/* Explicit nul termination */
	if (igbinary_serialize8(&igsd, 0 TSRMLS_CC) != 0) {
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		return 1;
	}

	/* shrink buffer to the real length, ignore errors */
	tmpbuf = (uint8_t *) igsd.mm.realloc(igsd.buffer, igsd.buffer_size, igsd.mm.context);
	if (tmpbuf != NULL) {
		igsd.buffer = tmpbuf;
	}

	/* Set return values */
	*ret_len = igsd.buffer_size - 1;
	*ret = igsd.buffer;

	igbinary_serialize_data_deinit(&igsd, 0 TSRMLS_CC);

	return 0;
}
/* }}} */
/* {{{ int igbinary_unserialize(const uint8_t *, size_t, zval *) */
IGBINARY_API int igbinary_unserialize(const uint8_t *buf, size_t buf_len, zval *z TSRMLS_DC) {
	struct igbinary_unserialize_data igsd;

	igbinary_unserialize_data_init(&igsd TSRMLS_CC);

	igsd.buffer = (uint8_t *) buf;
	igsd.buffer_size = buf_len;

	if (igbinary_unserialize_header(&igsd TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return 1;
	}

	if (igbinary_unserialize_zval(&igsd, z TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return 1;
	}

	igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);

	return 0;
}
/* }}} */
/* {{{ proto string igbinary_unserialize(mixed value) */
PHP_FUNCTION(igbinary_unserialize) {
	zend_string *string;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &string) == FAILURE) {
		RETURN_NULL();
	}

	if (string->len <= 0) {
		RETURN_FALSE;
	}

	if (igbinary_unserialize((uint8_t *) string->val, string->len, return_value TSRMLS_CC) != 0) {
		RETURN_NULL();
	}
}
/* }}} */
/* {{{ proto mixed igbinary_serialize(string value) */
PHP_FUNCTION(igbinary_serialize) {
	zval *z;
	uint8_t *string;
	size_t string_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &z) == FAILURE) {
		RETURN_NULL();
	}

	if (igbinary_serialize(&string, &string_len, z TSRMLS_CC) != 0) {
		RETURN_NULL();
	}

	RETVAL_STRINGL((char *)string, string_len);
	efree(string);
}
/* }}} */
#ifdef HAVE_PHP_SESSION
/* {{{ Serializer encode function */
PS_SERIALIZER_ENCODE_FUNC(igbinary)
{
	struct igbinary_serialize_data igsd;
	uint8_t *tmpbuf;
	zend_string *buf = NULL;

	if (igbinary_serialize_data_init(&igsd, false, NULL TSRMLS_CC)) {
		zend_error(E_WARNING, "igbinary_serialize: cannot init igsd");
		return buf;
	}

	if (igbinary_serialize_header(&igsd TSRMLS_CC) != 0) {
		zend_error(E_WARNING, "igbinary_serialize: cannot write header");
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		return buf;
	}

	if (igbinary_serialize_array(&igsd, &PS(http_session_vars), false, false TSRMLS_CC) != 0) {
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		return buf;
	}

	if (igbinary_serialize8(&igsd, 0 TSRMLS_CC) != 0) {
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		return buf;
	}

	/* shrink buffer to the real length, ignore errors */
	tmpbuf = (uint8_t *)igsd.mm.realloc(igsd.buffer, igsd.buffer_size, igsd.mm.context);
	if (tmpbuf != NULL) {
		igsd.buffer = tmpbuf;
	}

	buf = zend_string_init((char *)igsd.buffer, igsd.buffer_size, 0);
	igbinary_serialize_data_deinit(&igsd, 0 TSRMLS_CC);
	efree(igsd.buffer);
	return buf;
}
/* }}} */
/* {{{ Serializer decode function */
PS_SERIALIZER_DECODE_FUNC(igbinary) {
	zend_string *str_key;
	zval z, *d;

	struct igbinary_unserialize_data igsd;

	if (!val || vallen==0)
		return SUCCESS;

	if (igbinary_unserialize_data_init(&igsd TSRMLS_CC) != 0) {
		return FAILURE;
	}

	igsd.buffer = (uint8_t *)val;
	igsd.buffer_size = vallen;

	if (igbinary_unserialize_header(&igsd TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return FAILURE;
	}

	if (igbinary_unserialize_zval(&igsd, &z)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		zval_dtor(&z);
		return FAILURE;
	}

	igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);

	ZEND_HASH_FOREACH_STR_KEY_VAL(HASH_OF(&z), str_key, d) {
		if (str_key) {
			php_set_session_var(str_key, d, NULL);
			php_add_session_var(str_key);
		} else {
			/* ??? */
		}
	} ZEND_HASH_FOREACH_END();
	zval_dtor(&z);
	return SUCCESS;
}
/* }}} */
#endif /* HAVE_PHP_SESSION */

#if defined(HAVE_APC_SUPPORT) || defined(HAVE_APCU_SUPPORT)
/* {{{ apc_serialize function */
static int APC_SERIALIZER_NAME(igbinary) ( APC_SERIALIZER_ARGS ) {
	(void)config;

	if (igbinary_serialize(buf, buf_len, (zval *)value TSRMLS_CC) == 0) {
		/* flipped semantics */
		return 1;
	}
	return 0;
}
/* }}} */
/* {{{ apc_unserialize function */
static int APC_UNSERIALIZER_NAME(igbinary) ( APC_UNSERIALIZER_ARGS ) {
	if (igbinary_unserialize(buf, buf_len, &value) == 0) {
		/* flipped semantics */
		return 1;
	}
	zval_dtor(*value);
	ZVALL_NULL(*value);
	return 0;
}
/* }}} */
#endif

/* {{{ igbinary_serialize_data_init */
/** Inits igbinary_serialize_data. */
inline static int igbinary_serialize_data_init(struct igbinary_serialize_data *igsd, bool scalar, struct igbinary_memory_manager *memory_manager TSRMLS_DC) {
	int r = 0;

	if (memory_manager == NULL) {
		igsd->mm.alloc = igbinary_mm_wrapper_malloc;
		igsd->mm.realloc = igbinary_mm_wrapper_realloc;
		igsd->mm.free = igbinary_mm_wrapper_free;
		igsd->mm.context = NULL;
	} else {
		igsd->mm = *memory_manager;
	}

	igsd->buffer = NULL;
	igsd->buffer_size = 0;
	igsd->buffer_capacity = 32;
	igsd->string_count = 0;
	igsd->error = 0;

	igsd->buffer = (uint8_t *) igsd->mm.alloc(igsd->buffer_capacity, igsd->mm.context);
	if (igsd->buffer == NULL) {
		return 1;
	}

	igsd->scalar = scalar;
	if (!igsd->scalar) {
		hash_si_init(&igsd->strings, 16);
		hash_si_init(&igsd->objects, 16);
	}

	igsd->compact_strings = (bool)IGBINARY_G(compact_strings);

	return r;
}
/* }}} */
/* {{{ igbinary_serialize_data_deinit */
/** Deinits igbinary_serialize_data. */
inline static void igbinary_serialize_data_deinit(struct igbinary_serialize_data *igsd, int free_buffer TSRMLS_DC) {
	if (free_buffer && igsd->buffer) {
		igsd->mm.free(igsd->buffer, igsd->mm.context);
	}

	if (!igsd->scalar) {
		hash_si_deinit(&igsd->strings);
		hash_si_deinit(&igsd->objects);
	}
}
/* }}} */
/* {{{ igbinary_serialize_header */
/** Serializes header. */
inline static int igbinary_serialize_header(struct igbinary_serialize_data *igsd TSRMLS_DC) {
	return igbinary_serialize32(igsd, IGBINARY_FORMAT_VERSION TSRMLS_CC); /* version */
}
/* }}} */
/* {{{ igbinary_serialize_resize */
/** Expands igbinary_serialize_data. */
inline static int igbinary_serialize_resize(struct igbinary_serialize_data *igsd, size_t size TSRMLS_DC) {
	if (igsd->buffer_size + size < igsd->buffer_capacity) {
		return 0;
	}

	while (igsd->buffer_size + size >= igsd->buffer_capacity) {
		igsd->buffer_capacity *= 2;
	}

	igsd->buffer = (uint8_t *) igsd->mm.realloc(igsd->buffer, igsd->buffer_capacity, igsd->mm.context);
	if (igsd->buffer == NULL)
		return 1;

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize8 */
/** Serialize 8bit value. */
inline static int igbinary_serialize8(struct igbinary_serialize_data *igsd, uint8_t i TSRMLS_DC) {
	if (igbinary_serialize_resize(igsd, 1 TSRMLS_CC)) {
		return 1;
	}

	igsd->buffer[igsd->buffer_size++] = i;
	return 0;
}
/* }}} */
/* {{{ igbinary_serialize16 */
/** Serialize 16bit value. */
inline static int igbinary_serialize16(struct igbinary_serialize_data *igsd, uint16_t i TSRMLS_DC) {
	if (igbinary_serialize_resize(igsd, 2 TSRMLS_CC)) {
		return 1;
	}

	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 8 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i & 0xff);

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize32 */
/** Serialize 32bit value. */
inline static int igbinary_serialize32(struct igbinary_serialize_data *igsd, uint32_t i TSRMLS_DC) {
	if (igbinary_serialize_resize(igsd, 4 TSRMLS_CC)) {
		return 1;
	}

	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 24 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 16 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 8 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i & 0xff);

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize64 */
/** Serialize 64bit value. */
inline static int igbinary_serialize64(struct igbinary_serialize_data *igsd, uint64_t i TSRMLS_DC) {
	if (igbinary_serialize_resize(igsd, 8 TSRMLS_CC)) {
		return 1;
	}

	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 56 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 48 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 40 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 32 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 24 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 16 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i >> 8 & 0xff);
	igsd->buffer[igsd->buffer_size++] = (uint8_t) (i & 0xff);

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_null */
/** Serializes null. */
inline static int igbinary_serialize_null(struct igbinary_serialize_data *igsd TSRMLS_DC) {
	return igbinary_serialize8(igsd, igbinary_type_null TSRMLS_CC);
}
/* }}} */
/* {{{ igbinary_serialize_bool */
/** Serializes bool. */
inline static int igbinary_serialize_bool(struct igbinary_serialize_data *igsd, int b TSRMLS_DC) {
	return igbinary_serialize8(igsd, (uint8_t) (b ? igbinary_type_bool_true : igbinary_type_bool_false) TSRMLS_CC);
}
/* }}} */
/* {{{ igbinary_serialize_long */
/** Serializes long. */
inline static int igbinary_serialize_long(struct igbinary_serialize_data *igsd, long l TSRMLS_DC) {
	long k = l >= 0 ? l : -l;
	bool p = l >= 0 ? true : false;

	/* -LONG_MIN is 0 otherwise. */
	if (l == LONG_MIN) {
#if SIZEOF_LONG == 8
		if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_long64n TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize64(igsd, (uint64_t) 0x8000000000000000 TSRMLS_CC) != 0) {
			return 1;
		}
#elif SIZEOF_LONG == 4
		if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_long32n TSRMLS_CC) != 0) {
			return 1;
		}
		if (igbinary_serialize32(igsd, (uint32_t) 0x80000000 TSRMLS_CC) != 0) {
			return 1;
		}
#else
#error "Strange sizeof(long)."
#endif
		return 0;
	}

	if (k <= 0xff) {
		if (igbinary_serialize8(igsd, (uint8_t) (p ? igbinary_type_long8p : igbinary_type_long8n) TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize8(igsd, (uint8_t) k TSRMLS_CC) != 0) {
			return 1;
		}
	} else if (k <= 0xffff) {
		if (igbinary_serialize8(igsd, (uint8_t) (p ? igbinary_type_long16p : igbinary_type_long16n) TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize16(igsd, (uint16_t) k TSRMLS_CC) != 0) {
			return 1;
		}
#if SIZEOF_LONG == 8
	} else if (k <= 0xffffffff) {
		if (igbinary_serialize8(igsd, (uint8_t) (p ? igbinary_type_long32p : igbinary_type_long32n) TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize32(igsd, (uint32_t) k TSRMLS_CC) != 0) {
			return 1;
		}
	} else {
		if (igbinary_serialize8(igsd, (uint8_t) (p ? igbinary_type_long64p : igbinary_type_long64n) TSRMLS_CC) != 0) {
			return 1;
		}
		if (igbinary_serialize64(igsd, (uint64_t) k TSRMLS_CC) != 0) {
			return 1;
		}
	}
#elif SIZEOF_LONG == 4
	} else {
		if (igbinary_serialize8(igsd, (uint8_t) (p ? igbinary_type_long32p : igbinary_type_long32n) TSRMLS_CC) != 0) {
			return 1;
		}
		if (igbinary_serialize32(igsd, (uint32_t) k TSRMLS_CC) != 0) {
			return 1;
		}
	}
#else
#error "Strange sizeof(long)."
#endif

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_double */
/** Serializes double. */
inline static int igbinary_serialize_double(struct igbinary_serialize_data *igsd, double d TSRMLS_DC) {
	union {
		double d;
		uint64_t u;
	} u;

	if (igbinary_serialize8(igsd, igbinary_type_double TSRMLS_CC) != 0) {
		return 1;
	}

	u.d = d;

	return igbinary_serialize64(igsd, u.u TSRMLS_CC);
}
/* }}} */
/* {{{ igbinary_serialize_string */
/** Serializes string.
 * Serializes each string once, after first time uses pointers.
 */
inline static int igbinary_serialize_string(struct igbinary_serialize_data *igsd, char *s, size_t len TSRMLS_DC) {
	uint32_t t;
	uint32_t *i = &t;

	if (len == 0) {
		if (igbinary_serialize8(igsd, igbinary_type_string_empty TSRMLS_CC) != 0) {
			return 1;
		}

		return 0;
	}

	if (igsd->scalar || !igsd->compact_strings || hash_si_find(&igsd->strings, s, len, i) == 1) {
		if (!igsd->scalar && igsd->compact_strings) {
			hash_si_insert(&igsd->strings, s, len, igsd->string_count);
		}

		igsd->string_count += 1;

		if (igbinary_serialize_chararray(igsd, s, len TSRMLS_CC) != 0) {
			return 1;
		}
	} else {
		if (*i <= 0xff) {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_string_id8 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize8(igsd, (uint8_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		} else if (*i <= 0xffff) {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_string_id16 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize16(igsd, (uint16_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		} else {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_string_id32 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize32(igsd, (uint32_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_chararray */
/** Serializes string data. */
inline static int igbinary_serialize_chararray(struct igbinary_serialize_data *igsd, const char *s, size_t len TSRMLS_DC) {
	if (len <= 0xff) {
		if (igbinary_serialize8(igsd, igbinary_type_string8 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize8(igsd, len TSRMLS_CC) != 0) {
			return 1;
		}
	} else if (len <= 0xffff) {
		if (igbinary_serialize8(igsd, igbinary_type_string16 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize16(igsd, len TSRMLS_CC) != 0) {
			return 1;
		}
	} else {
		if (igbinary_serialize8(igsd, igbinary_type_string32 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize32(igsd, len TSRMLS_CC) != 0) {
			return 1;
		}
	}

	if (igbinary_serialize_resize(igsd, len TSRMLS_CC)) {
		return 1;
	}

	memcpy(igsd->buffer+igsd->buffer_size, s, len);
	igsd->buffer_size += len;

	return 0;
}
/* }}} */
/* {{{ igbinay_serialize_array */
/** Serializes array or objects inner properties. */
inline static int igbinary_serialize_array(struct igbinary_serialize_data *igsd, zval *z, bool object, bool incomplete_class TSRMLS_DC) {
	HashTable *h;
	HashPosition pos;
	size_t n;
	zval *d;

	zend_string *str_key;
	int key_type;
	ulong key_index;

	/* hash */
	h = object ? Z_OBJPROP_P(z) : HASH_OF(z);

	/* hash size */
	n = h ? zend_hash_num_elements(h) : 0;

	/* incomplete class magic member */
	if (n > 0 && incomplete_class) {
		--n;
	}

	if (!object && igbinary_serialize_array_ref(igsd, z, object TSRMLS_CC) == 0) {
		return 0;
	}

	if (n <= 0xff) {
		if (igbinary_serialize8(igsd, igbinary_type_array8 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize8(igsd, n TSRMLS_CC) != 0) {
			return 1;
		}
	} else if (n <= 0xffff) {
		if (igbinary_serialize8(igsd, igbinary_type_array16 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize16(igsd, n TSRMLS_CC) != 0) {
			return 1;
		}
	} else {
		if (igbinary_serialize8(igsd, igbinary_type_array32 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize32(igsd, n TSRMLS_CC) != 0) {
			return 1;
		}
	}

	if (n == 0) {
		return 0;
	}

	/* serialize properties. */
	zend_hash_internal_pointer_reset_ex(h, &pos);
	for (;; zend_hash_move_forward_ex(h, &pos)) {
		key_type = zend_hash_get_current_key_ex(h, &str_key, &key_index, &pos);

		/* last */
		if (key_type == HASH_KEY_NON_EXISTENT) {
			break;
		}

		/* skip magic member in incomplete classes */
		if (incomplete_class && strcmp(str_key->val, MAGIC_MEMBER) == 0) {
			continue;
		}

		switch (key_type) {
			case HASH_KEY_IS_LONG:
				if (igbinary_serialize_long(igsd, key_index TSRMLS_CC) != 0) {
					return 1;
				}
				break;

			case HASH_KEY_IS_STRING:
				if (igbinary_serialize_string(igsd, str_key->val, str_key->len) != 0) {
					return 1;
				}
				break;

			default:
				zend_error(E_ERROR, "igbinary_serialize_array: key is not string nor array");
				/* not reached */
				return 1;
		}

		/* we should still add element even if it's not OK,
		 * since we already wrote the length of the array before */
		if ((d = zend_hash_get_current_data_ex(h, &pos)) == NULL) {
			if (igbinary_serialize_null(igsd TSRMLS_CC)) {
				return 1;
			}
		} else {
			if (igbinary_serialize_zval(igsd, d TSRMLS_CC)) {
				return 1;
			}
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_array_ref */
/** Serializes array reference. */
inline static int igbinary_serialize_array_ref(struct igbinary_serialize_data *igsd, zval *z, bool object TSRMLS_DC) {
	uint32_t t = 0;
	uint32_t *i = &t;
	union {
		zval *z;
		struct {
			zend_class_entry *ce;
			// TODO Sean-Der
			//zend_object_handle handle;
		} obj;
	} key = { 0 };

	if (object && Z_TYPE_P(z) == IS_OBJECT /* TODO Sean-Der && Z_OBJ_HT_P(z)->get_class_entry */) {
		key.obj.ce = Z_OBJCE_P(z);
		// TODO Sean-Der
		// key.obj.handle = Z_OBJ_HANDLE_P(z);
	} else {
		key.z = z;
	}

	if (hash_si_find(&igsd->objects, (char *)&key, sizeof(key), i) == 1) {
		t = hash_si_size(&igsd->objects);
		hash_si_insert(&igsd->objects, (char *)&key, sizeof(key), t);
		return 1;
	} else {
		enum igbinary_type type;
		if (*i <= 0xff) {
			type = object ? igbinary_type_objref8 : igbinary_type_ref8;
			if (igbinary_serialize8(igsd, (uint8_t) type TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize8(igsd, (uint8_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		} else if (*i <= 0xffff) {
			type = object ? igbinary_type_objref16 : igbinary_type_ref16;
			if (igbinary_serialize8(igsd, (uint8_t) type TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize16(igsd, (uint16_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		} else {
			type = object ? igbinary_type_objref32 : igbinary_type_ref32;
			if (igbinary_serialize8(igsd, (uint8_t) type TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize32(igsd, (uint32_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		}

		return 0;
	}

	return 1;
}
/* }}} */
/* {{{ igbinary_serialize_array_sleep */
/** Serializes object's properties array with __sleep -function. */
inline static int igbinary_serialize_array_sleep(struct igbinary_serialize_data *igsd, zval *z, HashTable *h, zend_class_entry *ce, bool incomplete_class TSRMLS_DC) {
	HashPosition pos;
	size_t n = zend_hash_num_elements(h);
	zval *d, *v;
	zend_string *d_zstring;

	zend_string *str_key;
	int key_type;
	ulong key_index;

	/* Decrease array size by one, because of magic member (with class name) */
	if (n > 0 && incomplete_class) {
		--n;
	}

	/* Serialize array id. */
	if (n <= 0xff) {
		if (igbinary_serialize8(igsd, igbinary_type_array8 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize8(igsd, n TSRMLS_CC) != 0) {
			return 1;
		}
	} else if (n <= 0xffff) {
		if (igbinary_serialize8(igsd, igbinary_type_array16 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize16(igsd, n TSRMLS_CC) != 0) {
			return 1;
		}
	} else {
		if (igbinary_serialize8(igsd, igbinary_type_array32 TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize32(igsd, n TSRMLS_CC) != 0) {
			return 1;
		}
	}

	if (n == 0) {
		return 0;
	}

	zend_hash_internal_pointer_reset_ex(h, &pos);

	for (;; zend_hash_move_forward_ex(h, &pos)) {
		key_type = zend_hash_get_current_key_ex(h, &str_key, &key_index, &pos);

		/* last */
		if (key_type == HASH_KEY_NON_EXISTENT) {
			break;
		}

		/* skip magic member in incomplete classes */
		if (incomplete_class && strcmp(str_key->val, MAGIC_MEMBER) == 0) {
			continue;
		}

		if ((d = zend_hash_get_current_data_ex(h, &pos)) == NULL || Z_TYPE_P(d) != IS_STRING) {
			php_error_docref(NULL TSRMLS_CC, E_NOTICE, "__sleep should return an array only "
					"containing the names of instance-variables to "
					"serialize");

			/* we should still add element even if it's not OK,
			 * since we already wrote the length of the array before
			 * serialize null as key-value pair */
			if (igbinary_serialize_null(igsd TSRMLS_CC) != 0) {
				return 1;
			}
		} else {
			d_zstring = zval_get_string(d);
			if ((v = zend_hash_find(Z_OBJPROP_P(z), d_zstring)) != NULL) {
				if (igbinary_serialize_string(igsd, Z_STRVAL_P(d), Z_STRLEN_P(d)) != 0) {
					return 1;
				}

				if (igbinary_serialize_zval(igsd, v) != 0) {
					return 1;
				}
			} else if (ce) {
				zend_string *priv_name, *prot_name;

				do {
					/* try private */

					priv_name = zend_mangle_property_name(ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), Z_STRVAL_P(d), Z_STRLEN_P(d), ce->type & ZEND_INTERNAL_CLASS);
					if ((v = zend_hash_find(Z_OBJPROP_P(z), priv_name)) != NULL) {
						if (igbinary_serialize_string(igsd, ZSTR_VAL(priv_name), ZSTR_LEN(priv_name)) != 0) {
							zend_string_release(priv_name);
							return 1;
						}

						zend_string_release(priv_name);
						if (igbinary_serialize_zval(igsd, v) != 0) {
							return 1;
						}
						break;
					}
					efree(priv_name);

					/* try protected */
					prot_name = zend_mangle_property_name("*", 1, Z_STRVAL_P(d), Z_STRLEN_P(d), ce->type & ZEND_INTERNAL_CLASS);
					if ((v = zend_hash_find(Z_OBJPROP_P(z), prot_name)) != NULL) {
						if (igbinary_serialize_string(igsd, ZSTR_VAL(prot_name), ZSTR_LEN(prot_name)) != 0) {
							efree(prot_name);
							return 1;
						}

						efree(prot_name);
						if (igbinary_serialize_zval(igsd, v) != 0) {
							return 1;
						}
						break;
					}
					efree(prot_name);

					/* no win */
					php_error_docref(NULL TSRMLS_CC, E_NOTICE, "\"%s\" returned as member variable from __sleep() but does not exist", Z_STRVAL_P(d));
					if (igbinary_serialize_string(igsd, Z_STRVAL_P(d), Z_STRLEN_P(d)) != 0) {
						return 1;
					}

					if (igbinary_serialize_null(igsd) != 0) {
						return 1;
					}
				} while (0);

			} else {
				// if all else fails, just serialize the value in anyway.
				if (igbinary_serialize_string(igsd, Z_STRVAL_P(d), Z_STRLEN_P(d)) != 0) {
					return 1;
				}

				if (igbinary_serialize_zval(igsd, v) != 0) {
					return 1;
				}
			}
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_object_name */
/** Serialize object name. */
inline static int igbinary_serialize_object_name(struct igbinary_serialize_data *igsd, const char *class_name, size_t name_len TSRMLS_DC) {
	uint32_t t;
	uint32_t *i = &t;

	if (hash_si_find(&igsd->strings, class_name, name_len, i) == 1) {
		hash_si_insert(&igsd->strings, class_name, name_len, igsd->string_count);
		igsd->string_count += 1;

		if (name_len <= 0xff) {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object8 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize8(igsd, (uint8_t) name_len TSRMLS_CC) != 0) {
				return 1;
			}
		} else if (name_len <= 0xffff) {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object16 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize16(igsd, (uint16_t) name_len TSRMLS_CC) != 0) {
				return 1;
			}
		} else {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object32 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize32(igsd, (uint32_t) name_len TSRMLS_CC) != 0) {
				return 1;
			}
		}

		if (igbinary_serialize_resize(igsd, name_len TSRMLS_CC)) {
			return 1;
		}

		memcpy(igsd->buffer+igsd->buffer_size, class_name, name_len);
		igsd->buffer_size += name_len;
	} else {
		/* already serialized string */
		if (*i <= 0xff) {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object_id8 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize8(igsd, (uint8_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		} else if (*i <= 0xffff) {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object_id16 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize16(igsd, (uint16_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		} else {
			if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object_id32 TSRMLS_CC) != 0) {
				return 1;
			}

			if (igbinary_serialize32(igsd, (uint32_t) *i TSRMLS_CC) != 0) {
				return 1;
			}
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_object */
/** Serialize object.
 * @see ext/standard/var.c
 * */
inline static int igbinary_serialize_object(struct igbinary_serialize_data *igsd, zval *z TSRMLS_DC) {
	zend_class_entry *ce;
	zend_string *hash_key;
	zval f, h;

	int r = 0;

	unsigned char *serialized_data = NULL;
	size_t serialized_len;

	PHP_CLASS_ATTRIBUTES;

	if (igbinary_serialize_array_ref(igsd, z, true TSRMLS_CC) == 0) {
		return r;
	}

	ce = Z_OBJCE_P(z);

	/* custom serializer */
	if (ce && ce->serialize != NULL) {
		if (ce->serialize(z, &serialized_data, &serialized_len, (zend_serialize_data *)NULL TSRMLS_CC) == SUCCESS && !EG(exception) ) {
			if (igbinary_serialize_object_name(igsd, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name)) != 0) {
				if (serialized_data) {
					efree(serialized_data);
				}
				return 1;
			}


			if (serialized_len <= 0xff) {
				if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object_ser8 TSRMLS_CC) != 0) {
					if (serialized_data) {
						efree(serialized_data);
					}
					return 1;
				}

				if (igbinary_serialize8(igsd, (uint8_t) serialized_len TSRMLS_CC) != 0) {
					if (serialized_data) {
						efree(serialized_data);
					}
					return 1;
				}
			} else if (serialized_len <= 0xffff) {
				if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object_ser16 TSRMLS_CC) != 0) {
					if (serialized_data) {
						efree(serialized_data);
					}
					return 1;
				}

				if (igbinary_serialize16(igsd, (uint16_t) serialized_len TSRMLS_CC) != 0) {
					if (serialized_data) {
						efree(serialized_data);
					}
					return 1;
				}
			} else {
				if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_object_ser32 TSRMLS_CC) != 0) {
					if (serialized_data) {
						efree(serialized_data);
					}
					return 1;
				}

				if (igbinary_serialize32(igsd, (uint32_t) serialized_len TSRMLS_CC) != 0) {
					if (serialized_data) {
						efree(serialized_data);
					}
					return 1;
				}
			}

			if (igbinary_serialize_resize(igsd, serialized_len TSRMLS_CC)) {
				if (serialized_data) {
					efree(serialized_data);
				}

				return 1;
			}

			memcpy(igsd->buffer+igsd->buffer_size, serialized_data, serialized_len);
			igsd->buffer_size += serialized_len;
		} else if (EG(exception)) {
			/* exception, return failure */
			r = 1;
		} else {
			/* Serialization callback failed, assume null output */
			r = igbinary_serialize_null(igsd TSRMLS_CC);
		}

		if (serialized_data) {
			efree(serialized_data);
		}

		return r;
	}

	/* serialize class name */
	PHP_SET_CLASS_ATTRIBUTES(z);
	if (igbinary_serialize_object_name(igsd, ZSTR_VAL(class_name), ZSTR_LEN(class_name)) != 0) {
		PHP_CLEANUP_CLASS_ATTRIBUTES();
		return 1;
	}
	PHP_CLEANUP_CLASS_ATTRIBUTES();

	hash_key = zend_string_init("__sleep", sizeof("__sleep") - 1, 0);
	if (ce && ce != PHP_IC_ENTRY && zend_hash_exists(&ce->function_table, hash_key)) {
		/* function name string */
		ZVAL_STRINGL(&f, "__sleep", sizeof("__sleep") - 1);

		/* calling z->__sleep */
		r = call_user_function_ex(CG(function_table), z, &f, &h, 0, 0, 1, NULL TSRMLS_CC);

		if (r == SUCCESS && !EG(exception)) {
			r = 0;

			if (Z_TYPE_P(&h) == IS_UNDEF) {
				if (Z_TYPE_P(&h) == IS_ARRAY) {
					r = igbinary_serialize_array_sleep(igsd, z, HASH_OF(&h), ce, incomplete_class TSRMLS_CC);
				} else {
					php_error_docref(NULL TSRMLS_CC, E_NOTICE, "__sleep should return an array only "
							"containing the names of instance-variables to "
							"serialize");

					/* empty array */
					r = igbinary_serialize8(igsd, igbinary_type_array8 TSRMLS_CC);
					if (r == 0) {
						r = igbinary_serialize8(igsd, 0 TSRMLS_CC);
					}
				}
			}
		} else {
			r = 1;
		}

		/* cleanup */
		zval_ptr_dtor(&h);
		zend_string_release(hash_key);
		return r;
	} else {
		zend_string_release(hash_key);
		return igbinary_serialize_array(igsd, z, true, incomplete_class TSRMLS_CC);
	}
}
/* }}} */
/* {{{ igbinary_serialize_zval */
/** Serialize zval. */
static int igbinary_serialize_zval(struct igbinary_serialize_data *igsd, zval *z TSRMLS_DC) {
	if (Z_TYPE_P(z) == IS_INDIRECT) {
		z = Z_INDIRECT_P(z);
	}

	if (Z_ISREF_P(z)) {
		if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_ref TSRMLS_CC) != 0) {
			return 1;
		}

		/* Complex types serialize a reference, scalars do not... */
		/* FIXME: Absolutely wrong level to check this. */
		switch (Z_TYPE_P(z)) {
			case IS_RESOURCE:
			case IS_STRING:
			case IS_LONG:
			case IS_NULL:
			case IS_TRUE:
			case IS_FALSE:
			case IS_DOUBLE:
				/* Serialize a reference if zval already added */
				if (igbinary_serialize_array_ref(igsd, z, false TSRMLS_CC) == 0) {
					return 0;
				}
				/* otherwise fall through */
		}
	}
	switch (Z_TYPE_P(z)) {
		case IS_RESOURCE:
			return igbinary_serialize_null(igsd TSRMLS_CC);
		case IS_OBJECT:
			return igbinary_serialize_object(igsd, z TSRMLS_CC);
		case IS_ARRAY:
			return igbinary_serialize_array(igsd, z, false, false TSRMLS_CC);
		case IS_STRING:
			return igbinary_serialize_string(igsd, Z_STRVAL_P(z), Z_STRLEN_P(z) TSRMLS_CC);
		case IS_LONG:
			return igbinary_serialize_long(igsd, Z_LVAL_P(z) TSRMLS_CC);
		case IS_NULL:
			return igbinary_serialize_null(igsd TSRMLS_CC);
		case IS_TRUE:
			return igbinary_serialize_bool(igsd, 1);
		case IS_FALSE:
			return igbinary_serialize_bool(igsd, 0);
		case IS_DOUBLE:
			return igbinary_serialize_double(igsd, Z_DVAL_P(z) TSRMLS_CC);
		default:
			zend_error(E_ERROR, "igbinary_serialize_zval: zval has unknown type %d", (int)Z_TYPE_P(z));
			/* not reached */
			return 1;
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_data_init */
/** Inits igbinary_unserialize_data_init. */
inline static int igbinary_unserialize_data_init(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	smart_string empty_str = { 0 };

	igsd->buffer = NULL;
	igsd->buffer_size = 0;
	igsd->buffer_offset = 0;

	igsd->strings = NULL;
	igsd->strings_count = 0;
	igsd->strings_capacity = 4;
	igsd->string0_buf = empty_str;

	igsd->error = 0;
	igsd->references = NULL;
	igsd->references_count = 0;
	igsd->references_capacity = 4;

	igsd->references = (void **) emalloc(sizeof(void *) * igsd->references_capacity);
	if (igsd->references == NULL) {
		return 1;
	}

	igsd->strings = (struct igbinary_unserialize_string_pair *) emalloc(sizeof(struct igbinary_unserialize_string_pair) * igsd->strings_capacity);
	if (igsd->strings == NULL) {
		efree(igsd->references);
		return 1;
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_data_deinit */
/** Deinits igbinary_unserialize_data_init. */
inline static void igbinary_unserialize_data_deinit(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	if (igsd->strings) {
		efree(igsd->strings);
	}

	if (igsd->references) {
		efree(igsd->references);
	}

	smart_string_free(&igsd->string0_buf);

	return;
}
/* }}} */
/* {{{ igbinary_unserialize_header */
/** Unserialize header. Check for version. */
inline static int igbinary_unserialize_header(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	uint32_t version;

	if (igsd->buffer_offset + 4 >= igsd->buffer_size) {
		return 1;
	}

	version = igbinary_unserialize32(igsd TSRMLS_CC);

	/* Support older version 1 and the current format 2 */
	if (version == IGBINARY_FORMAT_VERSION || version == 0x00000001) {
		return 0;
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_header: unsupported version: %u, should be %u or %u", (unsigned int) version, 0x00000001, (unsigned int) IGBINARY_FORMAT_VERSION);
		return 1;
	}
}
/* }}} */
/* {{{ igbinary_unserialize8 */
/** Unserialize 8bit value. */
inline static uint8_t igbinary_unserialize8(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	uint8_t ret = 0;
	ret = igsd->buffer[igsd->buffer_offset++];
	return ret;
}
/* }}} */
/* {{{ igbinary_unserialize16 */
/** Unserialize 16bit value. */
inline static uint16_t igbinary_unserialize16(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	uint16_t ret = 0;
	ret |= ((uint16_t) igsd->buffer[igsd->buffer_offset++] << 8);
	ret |= ((uint16_t) igsd->buffer[igsd->buffer_offset++] << 0);
	return ret;
}
/* }}} */
/* {{{ igbinary_unserialize32 */
/** Unserialize 32bit value. */
inline static uint32_t igbinary_unserialize32(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	uint32_t ret = 0;
	ret |= ((uint32_t) igsd->buffer[igsd->buffer_offset++] << 24);
	ret |= ((uint32_t) igsd->buffer[igsd->buffer_offset++] << 16);
	ret |= ((uint32_t) igsd->buffer[igsd->buffer_offset++] << 8);
	ret |= ((uint32_t) igsd->buffer[igsd->buffer_offset++] << 0);
	return ret;
}
/* }}} */
/* {{{ igbinary_unserialize64 */
/** Unserialize 64bit value. */
inline static uint64_t igbinary_unserialize64(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	uint64_t ret = 0;
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 56);
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 48);
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 40);
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 32);
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 24);
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 16);
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 8);
	ret |= ((uint64_t) igsd->buffer[igsd->buffer_offset++] << 0);
	return ret;
}
/* }}} */
/* {{{ igbinary_unserialize_long */
/** Unserializes long */
inline static int igbinary_unserialize_long(struct igbinary_unserialize_data *igsd, enum igbinary_type t, long *ret TSRMLS_DC) {
	uint32_t tmp32;
#if SIZEOF_LONG == 8
	uint64_t tmp64;
#endif

	if (t == igbinary_type_long8p || t == igbinary_type_long8n) {
		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		*ret = (long) (t == igbinary_type_long8n ? -1 : 1) * igbinary_unserialize8(igsd TSRMLS_CC);
	} else if (t == igbinary_type_long16p || t == igbinary_type_long16n) {
		if (igsd->buffer_offset + 2 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		*ret = (long) (t == igbinary_type_long16n ? -1 : 1) * igbinary_unserialize16(igsd TSRMLS_CC);
	} else if (t == igbinary_type_long32p || t == igbinary_type_long32n) {
		if (igsd->buffer_offset + 4 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		/* check for boundaries */
		tmp32 = igbinary_unserialize32(igsd TSRMLS_CC);
#if SIZEOF_LONG == 4
		if (tmp32 > 0x80000000 || (tmp32 == 0x80000000 && t == igbinary_type_long32p)) {
			zend_error(E_WARNING, "igbinary_unserialize_long: 64bit long on 32bit platform?");
			tmp32 = 0; /* t == igbinary_type_long32p ? LONG_MAX : LONG_MIN; */
		}
#endif
		*ret = (long) (t == igbinary_type_long32n ? -1 : 1) * tmp32;
	} else if (t == igbinary_type_long64p || t == igbinary_type_long64n) {
#if SIZEOF_LONG == 8
		if (igsd->buffer_offset + 8 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		/* check for boundaries */
		tmp64 = igbinary_unserialize64(igsd TSRMLS_CC);
		if (tmp64 > 0x8000000000000000 || (tmp64 == 0x8000000000000000 && t == igbinary_type_long64p)) {
			zend_error(E_WARNING, "igbinary_unserialize_long: too big 64bit long.");
			tmp64 = 0; /* t == igbinary_type_long64p ? LONG_MAX : LONG_MIN */;
		}

		*ret = (long) (t == igbinary_type_long64n ? -1 : 1) * tmp64;
#elif SIZEOF_LONG == 4
		/* can't put 64bit long into 32bit one, placeholder zero */
		*ret = 0;
		igbinary_unserialize64(igsd TSRMLS_CC);
		zend_error(E_WARNING, "igbinary_unserialize_long: 64bit long on 32bit platform");
#else
#error "Strange sizeof(long)."
#endif
	} else {
		*ret = 0;
		zend_error(E_WARNING, "igbinary_unserialize_long: unknown type '%02x', position %zu", t, igsd->buffer_offset);
		return 1;
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_double */
/** Unserializes double. */
inline static int igbinary_unserialize_double(struct igbinary_unserialize_data *igsd, enum igbinary_type t, double *ret TSRMLS_DC) {
	union {
		double d;
		uint64_t u;
	} u;

	(void) t;

	if (igsd->buffer_offset + 8 > igsd->buffer_size) {
		zend_error(E_WARNING, "igbinary_unserialize_double: end-of-data");
		return 1;
	}


	u.u = igbinary_unserialize64(igsd TSRMLS_CC);

	*ret = u.d;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_string */
/** Unserializes string. Unserializes both actual string or by string id. */
inline static int igbinary_unserialize_string(struct igbinary_unserialize_data *igsd, enum igbinary_type t, char **s, size_t *len TSRMLS_DC) {
	size_t i;
	if (t == igbinary_type_string_id8 || t == igbinary_type_object_id8) {
		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_string: end-of-data");
			return 1;
		}
		i = igbinary_unserialize8(igsd TSRMLS_CC);
	} else if (t == igbinary_type_string_id16 || t == igbinary_type_object_id16) {
		if (igsd->buffer_offset + 2 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_string: end-of-data");
			return 1;
		}
		i = igbinary_unserialize16(igsd TSRMLS_CC);
	} else if (t == igbinary_type_string_id32 || t == igbinary_type_object_id32) {
		if (igsd->buffer_offset + 4 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_string: end-of-data");
			return 1;
		}
		i = igbinary_unserialize32(igsd TSRMLS_CC);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_string: unknown type '%02x', position %zu", t, igsd->buffer_offset);
		return 1;
	}

	if (i >= igsd->strings_count) {
		zend_error(E_WARNING, "igbinary_unserialize_string: string index is out-of-bounds");
		return 1;
	}

	*s = igsd->strings[i].data;
	*len = igsd->strings[i].len;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_chararray */
/** Unserializes chararray of string. */
inline static int igbinary_unserialize_chararray(struct igbinary_unserialize_data *igsd, enum igbinary_type t, char **s, size_t *len TSRMLS_DC) {
	size_t l;

	if (t == igbinary_type_string8 || t == igbinary_type_object8) {
		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return 1;
		}
		l = igbinary_unserialize8(igsd TSRMLS_CC);
		if (igsd->buffer_offset + l > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return 1;
		}
	} else if (t == igbinary_type_string16 || t == igbinary_type_object16) {
		if (igsd->buffer_offset + 2 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return 1;
		}
		l = igbinary_unserialize16(igsd TSRMLS_CC);
		if (igsd->buffer_offset + l > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return 1;
		}
	} else if (t == igbinary_type_string32 || t == igbinary_type_object32) {
		if (igsd->buffer_offset + 4 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return 1;
		}
		l = igbinary_unserialize32(igsd TSRMLS_CC);
		if (igsd->buffer_offset + l > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return 1;
		}
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_chararray: unknown type '%02x', position %zu", t, igsd->buffer_offset);
		return 1;
	}

	if (igsd->strings_count + 1 > igsd->strings_capacity) {
		while (igsd->strings_count + 1 > igsd->strings_capacity) {
			igsd->strings_capacity *= 2;
		}

		igsd->strings = (struct igbinary_unserialize_string_pair *) erealloc(igsd->strings, sizeof(struct igbinary_unserialize_string_pair) * igsd->strings_capacity);
		if (igsd->strings == NULL) {
			return 1;
		}
	}

	igsd->strings[igsd->strings_count].data = (char *) (igsd->buffer + igsd->buffer_offset);
	igsd->strings[igsd->strings_count].len = l;

	igsd->buffer_offset += l;

	if (igsd->strings[igsd->strings_count].data == NULL) {
		return 1;
	}

	*len = igsd->strings[igsd->strings_count].len;
	*s = igsd->strings[igsd->strings_count].data;

	igsd->strings_count += 1;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_array */
/** Unserializes array. */
inline static int igbinary_unserialize_array(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *z, int object TSRMLS_DC) {
	size_t n;
	size_t i;

	zval v;
	/*	zval *old_v; */

	char *key;
	size_t key_len = 0;
	long key_index = 0;

	enum igbinary_type key_type;

	HashTable *h;

	zend_string *igsd_zstr;

	if (t == igbinary_type_array8) {
		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
			return 1;
		}
		n = igbinary_unserialize8(igsd TSRMLS_CC);
	} else if (t == igbinary_type_array16) {
		if (igsd->buffer_offset + 2 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
			return 1;
		}
		n = igbinary_unserialize16(igsd TSRMLS_CC);
	} else if (t == igbinary_type_array32) {
		if (igsd->buffer_offset + 4 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
			return 1;
		}
		n = igbinary_unserialize32(igsd TSRMLS_CC);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_array: unknown type '%02x', position %zu", t, igsd->buffer_offset);
		return 1;
	}

	// n cannot be larger than the number of minimum "objects" in the array
	if (n > igsd->buffer_size - igsd->buffer_offset) {
		zend_error(E_WARNING, "%s: data size %zu smaller that requested array length %zu.", "igbinary_unserialize_array", igsd->buffer_size - igsd->buffer_offset, n);
		return 1;
	}

	if (!object) {
		array_init(z);
		// TODO Sean-Der
		//Z_TYPE_PP(z) = IS_ARRAY;
		//ALLOC_HASHTABLE(Z_ARRVAL_PP(z));
		//zend_hash_init(Z_ARRVAL_PP(z), n + 1, NULL, ZVAL_PTR_DTOR, 0);

		/* references */
		if (igsd->references_count + 1 >= igsd->references_capacity) {
			while (igsd->references_count + 1 >= igsd->references_capacity) {
				igsd->references_capacity *= 2;
			}

			igsd->references = (void **) erealloc(igsd->references, sizeof(void *) * igsd->references_capacity);
			if (igsd->references == NULL)
				return 1;
		}

		igsd->references[igsd->references_count++] = (void *) z;
	}

	/* empty array */
	if (n == 0) {
		return 0;
	}

	h = HASH_OF(z);

	for (i = 0; i < n; i++) {
		key = NULL;

		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
			zval_dtor(z);
			ZVAL_NULL(z);
			return 1;
		}

		key_type = (enum igbinary_type) igbinary_unserialize8(igsd TSRMLS_CC);

		switch (key_type) {
			case igbinary_type_long8p:
			case igbinary_type_long8n:
			case igbinary_type_long16p:
			case igbinary_type_long16n:
			case igbinary_type_long32p:
			case igbinary_type_long32n:
			case igbinary_type_long64p:
			case igbinary_type_long64n:
				if (igbinary_unserialize_long(igsd, key_type, &key_index TSRMLS_CC)) {
					zval_dtor(z);
					ZVAL_NULL(z);
					return 1;
				}
				break;
			case igbinary_type_string_id8:
			case igbinary_type_string_id16:
			case igbinary_type_string_id32:
				if (igbinary_unserialize_string(igsd, key_type, &key, &key_len TSRMLS_CC)) {
					zval_dtor(z);
					ZVAL_NULL(z);
					return 1;
				}
				break;
			case igbinary_type_string8:
			case igbinary_type_string16:
			case igbinary_type_string32:
				if (igbinary_unserialize_chararray(igsd, key_type, &key, &key_len TSRMLS_CC)) {
					zval_dtor(z);
					ZVAL_NULL(z);
					return 1;
				}
				break;
			case igbinary_type_string_empty:
				key = "";
				key_len = 0;
				break;
			case igbinary_type_null:
				continue;
			default:
				zend_error(E_WARNING, "igbinary_unserialize_array: unknown key type '%02x', position %zu", key_type, igsd->buffer_offset);
				zval_dtor(z);
				ZVAL_NULL(z);
				return 1;
		}


		if (igbinary_unserialize_zval(igsd, &v)) {
			zval_dtor(z);
			ZVAL_NULL(z);
			zval_ptr_dtor(&v);
			return 1;
		}

		if (key) {
			/* Keys must include a terminating null. */
			/* Ensure buffer starts at the beginning. */

			igsd->string0_buf.len = 0;
			smart_string_appendl(&igsd->string0_buf, key, key_len);
			smart_string_0(&igsd->string0_buf);
			igsd_zstr = zend_string_init(igsd->string0_buf.c, igsd->string0_buf.len, 0);
/*
			if (zend_symtable_find(h, key, key_len + 1, (void **)&old_v) == SUCCESS) {
				var_push_dtor(var_hash, old_v);
			}
*/
			zend_symtable_update(h, igsd_zstr, &v);
			zend_string_release(igsd_zstr);
		} else {
/*
			if (zend_hash_index_find(h, key_index, (void **)&old_v) == SUCCESS) {
				var_push_dtor(var_hash, old_v);
			}
*/
			zend_hash_index_update(h, key_index, &v);
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_object_ser */
/** Unserializes object's property array of objects implementing Serializable -interface. */
inline static int igbinary_unserialize_object_ser(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *z, zend_class_entry *ce TSRMLS_DC) {
	size_t n;
	int ret;
	php_unserialize_data_t var_hash;

	if (ce->unserialize == NULL) {
		zend_error(E_WARNING, "Class %s has no unserializer", ce->name);
		return 1;
	}

	if (t == igbinary_type_object_ser8) {
		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
			return 1;
		}
		n = igbinary_unserialize8(igsd TSRMLS_CC);
	} else if (t == igbinary_type_object_ser16) {
		if (igsd->buffer_offset + 2 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
			return 1;
		}
		n = igbinary_unserialize16(igsd TSRMLS_CC);
	} else if (t == igbinary_type_object_ser32) {
		if (igsd->buffer_offset + 4 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
			return 1;
		}
		n = igbinary_unserialize32(igsd TSRMLS_CC);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_object_ser: unknown type '%02x', position %zu", t, igsd->buffer_offset);
		return 1;
	}

	if (igsd->buffer_offset + n > igsd->buffer_size) {
		zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
		return 1;
	}

	PHP_VAR_UNSERIALIZE_INIT(var_hash);
	ret = ce->unserialize(z, ce,
		(const unsigned char*)(igsd->buffer + igsd->buffer_offset), n,
		(zend_unserialize_data *)&var_hash TSRMLS_CC);
	PHP_VAR_UNSERIALIZE_DESTROY(var_hash);

	if (ret != SUCCESS || EG(exception)) {
		return 1;
	}

	igsd->buffer_offset += n;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_object */
/** Unserialize object.
 * @see ext/standard/var_unserializer.c
 */
inline static int igbinary_unserialize_object(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *z TSRMLS_DC) {
	zend_class_entry *ce;
	zend_class_entry *pce;

	zval *h = NULL;
	zval f;

	char *name = NULL;
	size_t name_len = 0;
	zend_string *name_zstring, *hash_key;

	int r;

	bool incomplete_class = false;

	zval user_func, retval_ptr, args[1];

	if (t == igbinary_type_object8 || t == igbinary_type_object16 || t == igbinary_type_object32) {
		if (igbinary_unserialize_chararray(igsd, t, &name, &name_len TSRMLS_CC)) {
			return 1;
		}
	} else if (t == igbinary_type_object_id8 || t == igbinary_type_object_id16 || t == igbinary_type_object_id32) {
		if (igbinary_unserialize_string(igsd, t, &name, &name_len TSRMLS_CC)) {
			return 1;
		}
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_object: unknown object type '%02x', position %zu", t, igsd->buffer_offset);
		return 1;
	}

	do {
		/* Try to find class directly */
		name_zstring = zend_string_init(name, name_len, 0);
		if ((pce = zend_lookup_class(name_zstring)) != NULL) {
			ce = pce;
			zend_string_release(name_zstring);
			break;
		}
		zend_string_release(name_zstring);

		/* Check for unserialize callback */
		if ((PG(unserialize_callback_func) == NULL) || (PG(unserialize_callback_func)[0] == '\0')) {
			incomplete_class = 1;
			ce = PHP_IC_ENTRY;
			break;
		}

		/* Call unserialize callback */
		ZVAL_STRING(&user_func, PG(unserialize_callback_func));
		ZVAL_STRING(&args[0], name);
		if (call_user_function_ex(CG(function_table), NULL, &user_func, &retval_ptr, 1, args, 0, NULL TSRMLS_CC) != SUCCESS) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "defined (%s) but not found", name);
			incomplete_class = 1;
			ce = PHP_IC_ENTRY;
			zval_ptr_dtor(&user_func);
			zval_ptr_dtor(&args[0]);
			break;
		}
		//TODO Sean-Der
		//if (retval_ptr) {
		//	zval_ptr_dtor(&retval_ptr);
		//}

		/* The callback function may have defined the class */
		name_zstring = zend_string_init(name, name_len, 0);
		if ((pce = zend_lookup_class(name_zstring)) == SUCCESS) {
			ce = pce;
		} else {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Function %s() hasn't defined the class it was called for", name);
			incomplete_class = true;
			ce = PHP_IC_ENTRY;
		}
		zend_string_release(name_zstring);

		zval_ptr_dtor(&user_func);
		zval_ptr_dtor(&args[0]);
	} while (0);

	/* previous user function call may have raised an exception */
	if (EG(exception)) {
		return 1;
	}

	object_init_ex(z, ce);

	/* reference */
	if (igsd->references_count + 1 >= igsd->references_capacity) {
		while (igsd->references_count + 1 >= igsd->references_capacity) {
			igsd->references_capacity *= 2;
		}

		igsd->references = (void **) erealloc(igsd->references, sizeof(void *) * igsd->references_capacity);
		if (igsd->references == NULL)
			return 1;
	}

	igsd->references[igsd->references_count++] = (void *) z;

	/* store incomplete class name */
	if (incomplete_class) {
		php_store_class_name(z, name, name_len);
	}

	t = (enum igbinary_type) igbinary_unserialize8(igsd TSRMLS_CC);
	switch (t) {
		case igbinary_type_array8:
		case igbinary_type_array16:
		case igbinary_type_array32:
			r = igbinary_unserialize_array(igsd, t, z, 1 TSRMLS_CC);
			break;
		case igbinary_type_object_ser8:
		case igbinary_type_object_ser16:
		case igbinary_type_object_ser32:
			r = igbinary_unserialize_object_ser(igsd, t, z, ce TSRMLS_CC);
			break;
		default:
			zend_error(E_WARNING, "igbinary_unserialize_object: unknown object inner type '%02x', position %zu", t, igsd->buffer_offset);
			return 1;
	}

	if (r) {
		return r;
	}

	hash_key = zend_string_init("__wakeup", sizeof("__wakeup") - 1, 0);
	if (Z_OBJCE_P(z) != PHP_IC_ENTRY && zend_hash_exists(&Z_OBJCE_P(z)->function_table, hash_key)) {
		ZVAL_STRINGL(&f, "__wakeup", sizeof("__wakeup") - 1);
		call_user_function_ex(CG(function_table), z, &f, h, 0, 0, 1, NULL TSRMLS_CC);

		if (h) {
			zval_ptr_dtor(h);
		}

		if (EG(exception)) {
			r = 1;
		}
	}
	zend_string_release(hash_key);

	return r;
}
/* }}} */
/* {{{ igbinary_unserialize_ref */
/** Unserializes array or object by reference. */
inline static int igbinary_unserialize_ref(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval **z TSRMLS_DC) {
	size_t n;

	if (t == igbinary_type_ref8 || t == igbinary_type_objref8) {
		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_ref: end-of-data");
			return 1;
		}
		n = igbinary_unserialize8(igsd TSRMLS_CC);
	} else if (t == igbinary_type_ref16 || t == igbinary_type_objref16) {
		if (igsd->buffer_offset + 2 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_ref: end-of-data");
			return 1;
		}
		n = igbinary_unserialize16(igsd TSRMLS_CC);
	} else if (t == igbinary_type_ref32 || t == igbinary_type_objref32) {
		if (igsd->buffer_offset + 4 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_ref: end-of-data");
			return 1;
		}
		n = igbinary_unserialize32(igsd TSRMLS_CC);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_ref: unknown type '%02x', position %zu", t, igsd->buffer_offset);
		return 1;
	}

	if (n >= igsd->references_count) {
		zend_error(E_WARNING, "igbinary_unserialize_ref: invalid reference");
		return 1;
	}

	if (*z != NULL) {
		zval_ptr_dtor(*z);
	}

	*z = igsd->references[n];
	Z_ADDREF_P(*z);

	if (t == igbinary_type_objref8 || t == igbinary_type_objref16 || t == igbinary_type_objref32) {
	// TODO Sean-Der
	// Z_SET_ISREF_TO_P(z, false);
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_zval */
/** Unserialize zval. */
static int igbinary_unserialize_zval(struct igbinary_unserialize_data *igsd, zval *z) {
	enum igbinary_type t;

	long tmp_long;
	double tmp_double;
	char *tmp_chararray;
	size_t tmp_size_t;

	if (igsd->buffer_offset + 1 > igsd->buffer_size) {
		zend_error(E_WARNING, "igbinary_unserialize_zval: end-of-data");
		return 1;
	}

	t = (enum igbinary_type) igbinary_unserialize8(igsd TSRMLS_CC);

	switch (t) {
		case igbinary_type_ref:
			if (igbinary_unserialize_zval(igsd, z TSRMLS_CC)) {
				return 1;
			}
			/* Scalar types should be added to the references hash */
			/* unless they're already added */
			/* in references list: marked as ref */
			if (!Z_ISREF_P(z)) switch (Z_TYPE_P(z)) {
				case IS_STRING:
				case IS_LONG:
				case IS_NULL:
				case IS_DOUBLE:
				case IS_FALSE:
				case IS_TRUE:
					/* reference */
					if (igsd->references_count + 1 >= igsd->references_capacity) {
						while (igsd->references_count + 1 >= igsd->references_capacity) {
							igsd->references_capacity *= 2;
						}

						igsd->references = (void **) erealloc(igsd->references, sizeof(void *) * igsd->references_capacity);
						if (igsd->references == NULL)
							return 1;
					}

					igsd->references[igsd->references_count++] = (void *) z;
			}
			ZVAL_MAKE_REF(z);
			break;
		case igbinary_type_objref8:
		case igbinary_type_objref16:
		case igbinary_type_objref32:
		case igbinary_type_ref8:
		case igbinary_type_ref16:
		case igbinary_type_ref32:
			if (igbinary_unserialize_ref(igsd, t, &z TSRMLS_CC)) {
				return 1;
			}
			break;
		case igbinary_type_object8:
		case igbinary_type_object16:
		case igbinary_type_object32:
		case igbinary_type_object_id8:
		case igbinary_type_object_id16:
		case igbinary_type_object_id32:
			if (igbinary_unserialize_object(igsd, t, z TSRMLS_CC)) {
				return 1;
			}
			break;
		case igbinary_type_array8:
		case igbinary_type_array16:
		case igbinary_type_array32:
			if (igbinary_unserialize_array(igsd, t, z, 0 TSRMLS_CC)) {
				return 1;
			}
			break;
		case igbinary_type_string_empty:
			ZVAL_EMPTY_STRING(z);
			break;
		case igbinary_type_string_id8:
		case igbinary_type_string_id16:
		case igbinary_type_string_id32:
			if (igbinary_unserialize_string(igsd, t, &tmp_chararray, &tmp_size_t TSRMLS_CC)) {
				return 1;
			}
			ZVAL_STRINGL(z, tmp_chararray, tmp_size_t);
			break;
		case igbinary_type_string8:
		case igbinary_type_string16:
		case igbinary_type_string32:
			if (igbinary_unserialize_chararray(igsd, t, &tmp_chararray, &tmp_size_t TSRMLS_CC)) {
				return 1;
			}
			ZVAL_STRINGL(z, tmp_chararray, tmp_size_t);
			break;
		case igbinary_type_long8p:
		case igbinary_type_long8n:
		case igbinary_type_long16p:
		case igbinary_type_long16n:
		case igbinary_type_long32p:
		case igbinary_type_long32n:
		case igbinary_type_long64p:
		case igbinary_type_long64n:
			if (igbinary_unserialize_long(igsd, t, &tmp_long TSRMLS_CC)) {
				return 1;
			}
			ZVAL_LONG(z, tmp_long);
			break;
		case igbinary_type_null:
			ZVAL_NULL(z);
			break;
		case igbinary_type_bool_false:
			ZVAL_BOOL(z, 0);
			break;
		case igbinary_type_bool_true:
			ZVAL_BOOL(z, 1);
			break;
		case igbinary_type_double:
			if (igbinary_unserialize_double(igsd, t, &tmp_double TSRMLS_CC)) {
				return 1;
			}
			ZVAL_DOUBLE(z, tmp_double);
			break;
		default:
			zend_error(E_WARNING, "igbinary_unserialize_zval: unknown type '%02x', position %zu", t, igsd->buffer_offset);
			return 1;
	}

	return 0;
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
