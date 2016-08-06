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

#if PHP_MAJOR_VERSION >= 7
/* FIXME: still fix sessions and the APC-thingy */
# undef HAVE_APC_SUPPORT
# undef HAVE_APCU_SUPPORT
#endif

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
#include "hash_ptr.h"

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
	struct hash_si_ptr references;	/**< Hash of already serialized potential references. (non-NULL uintptr_t => int32_t) */
	int references_id;		/**< Number of things that the unserializer might think are references. >= length of references */
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

	zval **references;				/**< Unserialized Arrays/Objects. */
	size_t references_count;		/**< Unserialized array/objects count. */
	size_t references_capacity;		/**< Unserialized array/object array capacity. */

	zval *wakeup;					/**< zvals of type IS_OBJECT for calls to __wakeup. */
	size_t wakeup_count;			/**< count of objects in array for calls to __wakeup */
	size_t wakeup_capacity;			/**< capacity of objects in array for calls to __wakeup */

	int error;						/**< Error number. Not used. */
	smart_string string0_buf;			/**< Temporary buffer for strings */
};

#define IGB_REF_VAL(igsd, n)	((igsd)->references[(n)])

#define WANT_CLEAR     (0)
#define WANT_OBJECT    (1<<0)
#define WANT_REF       (1<<1)

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
inline static int igbinary_serialize_long(struct igbinary_serialize_data *igsd, zend_long l TSRMLS_DC);
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

inline static int igbinary_unserialize_long(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zend_long *ret TSRMLS_DC);
inline static int igbinary_unserialize_double(struct igbinary_unserialize_data *igsd, enum igbinary_type t, double *ret TSRMLS_DC);
inline static int igbinary_unserialize_string(struct igbinary_unserialize_data *igsd, enum igbinary_type t, char **s, size_t *len TSRMLS_DC);
inline static int igbinary_unserialize_chararray(struct igbinary_unserialize_data *igsd, enum igbinary_type t, char **s, size_t *len TSRMLS_DC);

inline static int igbinary_unserialize_array(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags TSRMLS_DC);
inline static int igbinary_unserialize_object(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags TSRMLS_DC);
inline static int igbinary_unserialize_object_ser(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, zend_class_entry *ce TSRMLS_DC);
inline static int igbinary_unserialize_ref(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags TSRMLS_DC);

static int igbinary_unserialize_zval(struct igbinary_unserialize_data *igsd, zval *const z, int flags TSRMLS_DC);
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
	PHP_FE_END
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
	ZEND_MOD_END
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
	 * Clean up ini entries.
	 * Aside: It seems like the php_session_register_serializer unserializes itself, since MSHUTDOWN in ext/wddx/wddx.c doesn't exist?
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

/* {{{ igsd management */
/* Append to list of references to take out later. Returns SIZE_MAX on allocation error. */
static inline size_t igsd_append_ref(struct igbinary_unserialize_data *igsd, zval *z)
{
	size_t ref_n;
	if (igsd->references_count + 1 >= igsd->references_capacity) {
		while (igsd->references_count + 1 >= igsd->references_capacity) {
			igsd->references_capacity *= 2;
		}

		igsd->references = erealloc(igsd->references, sizeof(igsd->references[0]) * igsd->references_capacity);
		if (igsd->references == NULL) {
			return SIZE_MAX;
		}
	}


	ref_n = igsd->references_count++;
	IGB_REF_VAL(igsd, ref_n) = z;
	return ref_n;
}

static inline int igsd_defer_wakeup(struct igbinary_unserialize_data *igsd, zval* z) {
	if (igsd->wakeup_count >= igsd->wakeup_capacity) {
		if (igsd->wakeup_capacity == 0) {
			igsd->wakeup_capacity = 2;
			igsd->wakeup = emalloc(sizeof(igsd->wakeup[0]) * igsd->wakeup_capacity);
		} else {
			igsd->wakeup_capacity *= 2;
			igsd->wakeup = erealloc(igsd->wakeup, sizeof(igsd->wakeup[0]) * igsd->wakeup_capacity);
			if (igsd->wakeup == NULL) {
				return 1;
			}
		}
	}

	ZVAL_COPY(&igsd->wakeup[igsd->wakeup_count], z);
	igsd->wakeup_count++;
	return 0;
}
/* }}} */

/* {{{ igbinary_finish_wakeup }}} */
static int igbinary_finish_wakeup(struct igbinary_unserialize_data* igsd TSRMLS_DC) {
	if (igsd->wakeup_count == 0) { /* nothing to do */
		return 0;
	}
	zval h;
	zval f;
	size_t i;
	ZVAL_STRINGL(&f, "__wakeup", sizeof("__wakeup") - 1);
	for (i = 0; i < igsd->wakeup_count; i++) {
		call_user_function_ex(CG(function_table), &(igsd->wakeup[i]), &f, &h, 0, 0, 1, NULL TSRMLS_CC);
		zval_ptr_dtor(&h);
		if (EG(exception)) {
			zval_dtor(&f);
			return 1;
		}
	}
	zval_dtor(&f);
	return 0;
}

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
    efree(ptr);
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
/* {{{ int igbinary_unserialize(const uint8_t *, size_t, zval **) */
IGBINARY_API int igbinary_unserialize(const uint8_t *buf, size_t buf_len, zval *z TSRMLS_DC) {
	struct igbinary_unserialize_data igsd;

	igbinary_unserialize_data_init(&igsd TSRMLS_CC);

	igsd.buffer = (uint8_t *) buf;
	igsd.buffer_size = buf_len;

	if (igbinary_unserialize_header(&igsd TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return 1;
	}

	if (igbinary_unserialize_zval(&igsd, z, WANT_CLEAR TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return 1;
	}

	if (igbinary_finish_wakeup(&igsd TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return 1;
	}
	igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);

	return 0;
}
/* }}} */
/* {{{ proto string igbinary_unserialize(mixed value) */
PHP_FUNCTION(igbinary_unserialize) {
	char *string = NULL;
	size_t string_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &string, &string_len) == FAILURE) {
		RETURN_NULL();
	}

	if (string_len <= 0) {
		RETURN_FALSE;
	}

	if (igbinary_unserialize((uint8_t *) string, string_len, return_value TSRMLS_CC) != 0) {
		/* FIXME: is this a good place? a catch all */
		zval_ptr_dtor(return_value);
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
	zend_string *result;
	zend_string *key;
	struct igbinary_serialize_data igsd;
	uint8_t *tmpbuf;

	if (igbinary_serialize_data_init(&igsd, false, NULL TSRMLS_CC)) {
		zend_error(E_WARNING, "igbinary_serialize: cannot init igsd");
		return zend_string_init("", 0, 0);
	}

	if (igbinary_serialize_header(&igsd TSRMLS_CC) != 0) {
		zend_error(E_WARNING, "igbinary_serialize: cannot write header");
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		return zend_string_init("", 0, 0);
	}

	if (igbinary_serialize_array(&igsd, &(PS(http_session_vars)), false, false TSRMLS_CC) != 0) {
		igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);
		zend_error(E_WARNING, "igbinary_serialize: cannot serialize session variables");
		return zend_string_init("", 0, 0);
	}

	/* Copy the buffer to a new zend_string */
	/* TODO: Clean up igsd->mm, and make this a pointer swap instead? It's only used for building up the serialization data buffer. */
	result = zend_string_init(igsd.buffer, igsd.buffer_size, 0);
	igbinary_serialize_data_deinit(&igsd, 1 TSRMLS_CC);

	return result;
}
/* }}} */
/* {{{ Serializer decode function */
PS_SERIALIZER_DECODE_FUNC(igbinary) {
	HashTable *tmp_hash;
	int tmp_int;
	zval z;
	zval *d;
	zend_string *key;

	struct igbinary_unserialize_data igsd;

	if (!val || vallen==0) {
		return SUCCESS;
	}

	if (igbinary_unserialize_data_init(&igsd TSRMLS_CC) != 0) {
		return FAILURE;
	}

	igsd.buffer = (uint8_t *)val;
	igsd.buffer_size = vallen;

	if (igbinary_unserialize_header(&igsd TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return FAILURE;
	}

	if (igbinary_unserialize_zval(&igsd, &z, WANT_CLEAR TSRMLS_CC)) {
		igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);
		return FAILURE;
	}

	igbinary_unserialize_data_deinit(&igsd TSRMLS_CC);

	tmp_hash = HASH_OF(&z);
	if (tmp_hash == NULL) {
		zval_ptr_dtor(&z);
		return FAILURE;
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(tmp_hash, key, d) {
		if (key == NULL) {  /* array key is a number, how? Skip it. */
			/* ??? */
			continue;
		}
		if (php_set_session_var(key, d, NULL TSRMLS_CC)) { /* Added to session successfully */
			/* Refcounted types such as arrays, objects, references need to have references incremented manually, so that zval_ptr_dtor doesn't clean up pointers they include. */
			/* Non-refcounted types have the data copied. */
			if (Z_REFCOUNTED_P(d)) {
				Z_ADDREF_P(d);
			}
		}
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&z);

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
	(void)config;

	if (igbinary_unserialize(buf, buf_len, value TSRMLS_CC) == 0) {
		/* flipped semantics */
		return 1;
	}
	zval_dtor(*value);
	(*value)->type = IS_NULL;
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
		hash_si_ptr_init(&igsd->references, 16);
		igsd->references_id = 0;
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
		hash_si_ptr_deinit(&igsd->references);
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
/** Serializes zend_long. */
inline static int igbinary_serialize_long(struct igbinary_serialize_data *igsd, zend_long l TSRMLS_DC) {
	zend_long k = l >= 0 ? l : -l;
	bool p = l >= 0 ? true : false;

	/* -ZEND_LONG_MIN is 0 otherwise. */
	if (l == ZEND_LONG_MIN) {
#if SIZEOF_ZEND_LONG == 8
		if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_long64n TSRMLS_CC) != 0) {
			return 1;
		}

		if (igbinary_serialize64(igsd, (uint64_t) 0x8000000000000000 TSRMLS_CC) != 0) {
			return 1;
		}
#elif SIZEOF_ZEND_LONG == 4
		if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_long32n TSRMLS_CC) != 0) {
			return 1;
		}
		if (igbinary_serialize32(igsd, (uint32_t) 0x80000000 TSRMLS_CC) != 0) {
			return 1;
		}
#else
#error "Strange sizeof(zend_long)."
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
#if SIZEOF_ZEND_LONG == 8
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
#elif SIZEOF_ZEND_LONG == 4
	} else {
		if (igbinary_serialize8(igsd, (uint8_t) (p ? igbinary_type_long32p : igbinary_type_long32n) TSRMLS_CC) != 0) {
			return 1;
		}
		if (igbinary_serialize32(igsd, (uint32_t) k TSRMLS_CC) != 0) {
			return 1;
		}
	}
#else
#error "Strange sizeof(zend_long)."
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
	/* If object=true: z is IS_OBJECT */
	/* If object=false: z is either IS_ARRAY, or IS_REFERENCE pointing to an IS_ARRAY. */
	HashTable *h;
	size_t n;
	zval *d;
	zval *z_original;

	zend_string *key;
	zend_long key_index;

	z_original = z;
	ZVAL_DEREF(z);

	/* hash */
	h = object ? Z_OBJPROP_P(z) : HASH_OF(z);

	/* hash size */
	n = h ? zend_hash_num_elements(h) : 0;

	/* incomplete class magic member */
	if (n > 0 && incomplete_class) {
		--n;
	}

	/* if it is an array or a reference to an array, then add a reference unique to that **reference** to that array */
	if (!object && igbinary_serialize_array_ref(igsd, z_original, false TSRMLS_CC) == 0) {
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
	ZEND_HASH_FOREACH_KEY_VAL(h, key_index, key, d) {
		/* skip magic member in incomplete classes */
		if (incomplete_class && strcmp(ZSTR_VAL(key), MAGIC_MEMBER) == 0) {
			continue;
		}

		if (key == NULL) {
			/* Key is numeric */
			if (igbinary_serialize_long(igsd, key_index TSRMLS_CC) != 0) {
				return 1;
			}
		} else {
			/* Key is string */
			if (igbinary_serialize_string(igsd, ZSTR_VAL(key), ZSTR_LEN(key) TSRMLS_CC) != 0) {
				return 1;
			}
		}

		if (d == NULL) {
			php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Received NULL value from hash.");
			return 1;
		}

		/* https://wiki.php.net/phpng-int - This is a weak pointer, completely different from a PHP reference (&$foo has a type of IS_REFERENCE) */
		if (Z_TYPE_P(d) == IS_INDIRECT) {
			d = Z_INDIRECT_P(d);
		}
		/* we should still add element even if it's not OK,
		 * since we already wrote the length of the array before */
		if (Z_TYPE_P(d) == IS_UNDEF) {
			if (igbinary_serialize_null(igsd TSRMLS_CC)) {
				return 1;
			}
		} else {
			if (igbinary_serialize_zval(igsd, d TSRMLS_CC)) {
				return 1;
			}
		}
	} ZEND_HASH_FOREACH_END();

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_array_ref */
/** Serializes array reference (or reference in an object). Returns 0 on success. */
inline static int igbinary_serialize_array_ref(struct igbinary_serialize_data *igsd, zval *z, bool object TSRMLS_DC) {
	uint32_t t = 0;
	uint32_t *i = &t;
	zend_uintptr_t key = 0;  /* The address of the pointer to the zend_refcounted struct or other struct */

	/* Similar to php_var_serialize_intern's first part, as well as php_add_var_hash, for printing R: (reference) or r:(object) */
	/* However, it differs from the built in serialize() in that references to objects are preserved when serializing and unserializing? (TODO check, test for backwards compatibility) */
	zend_bool is_ref = Z_ISREF_P(z);
	/* Do I have to dereference object references so that reference ids will be the same as in php5? */
	/* If I do, then more tests fail. */
	/* is_ref || IS_OBJECT implies it has a unique refcounted struct */
	if (object && Z_TYPE_P(z) == IS_OBJECT) {
          key = (zend_uintptr_t) Z_OBJ_HANDLE_P(z); /* expand object handle(uint32_t), cast to 32-bit/64-bit pointer */
	} else if (is_ref) {
		/* NOTE: PHP removed switched from `zval*` to `zval` for the values stored in HashTables. If an array has two references to the same ZVAL, then those references will have different zvals. We use Z_COUNTED_P(ref), which will be the same iff the references are the same */
	  	/* IS_REF implies there is a unique reference counting pointer for the reference */
	  	key = (zend_uintptr_t) Z_COUNTED_P(z);
	} else if (Z_TYPE_P(z) == IS_ARRAY) {
		if (Z_REFCOUNTED_P(z)) {
			key = (zend_uintptr_t) Z_COUNTED_P(z);
		} else { /* Not sure if this could be a constant */
			key = (zend_uintptr_t) z;
		}
	} else {
		/* Nothing else is going to reference this when this is serialized, this isn't ref counted or an object, shouldn't be reached. */
		/* Increment the reference id for the deserializer, give up. */
		++igsd->references_id;
                php_error_docref(NULL TSRMLS_CC, E_NOTICE, "igbinary_serialize_array_ref expected either object or reference (param object=%s), got neither (zend_type=%d)", object ? "true" : "false", (int)Z_TYPE_P(z));
		return 1;
	}

	if (hash_si_ptr_find(&igsd->references, key, i) == 1) {
		t = igsd->references_id++;
		/* FIXME hack? If the top-level element was an array, we assume that it can't be a reference when we serialize it, */
		/* because that's the way it was serialized in php5. */
		/* Does this work with different forms of recursive arrays? */
		if (t > 0 || object) {
			hash_si_ptr_insert(&igsd->references, key, t);  /* TODO: Add a specialization for fixed-length numeric keys? */
		}
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
	HashTable *object_properties;
	size_t n = zend_hash_num_elements(h);
	zval *d;
	zval *v;

	zend_string *key;

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

	object_properties = Z_OBJPROP_P(z);

	ZEND_HASH_FOREACH_STR_KEY_VAL(h, key, d) {
		/* skip magic member in incomplete classes */
		if (incomplete_class && key != NULL && strcmp(ZSTR_VAL(key), MAGIC_MEMBER) == 0) {
			continue;
		}

		if (d == NULL || Z_TYPE_P(d) != IS_STRING) {
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
			zend_string *prop_name = Z_STR_P(d);

			if ((v = zend_hash_find(object_properties, prop_name)) != NULL) {
				if (igbinary_serialize_string(igsd, ZSTR_VAL(prop_name), ZSTR_LEN(prop_name) TSRMLS_CC) != 0) {
					return 1;
				}

				if (Z_TYPE_P(v) == IS_INDIRECT) {
					v = Z_INDIRECT_P(v);
				}
				if (igbinary_serialize_zval(igsd, v TSRMLS_CC) != 0) {
					return 1;
				}
			} else if (ce) {
				zend_string *mangled_prop_name;

				v = NULL;

				do {
					/* try private */
					mangled_prop_name = zend_mangle_property_name(ZSTR_VAL(ce->name), ZSTR_LEN(ce->name),
						ZSTR_VAL(prop_name), ZSTR_LEN(prop_name), ce->type & ZEND_INTERNAL_CLASS);
					v = zend_hash_find(object_properties, mangled_prop_name);

					/* try protected */
					if (v == NULL) {
						zend_string_release(mangled_prop_name);
						mangled_prop_name = zend_mangle_property_name("*", 1,
							ZSTR_VAL(prop_name), ZSTR_LEN(prop_name), ce->type & ZEND_INTERNAL_CLASS);

						v = zend_hash_find(object_properties, mangled_prop_name);
					}

					/* Neither property exist */
					if (v == NULL) {
						zend_string_release(mangled_prop_name);

						php_error_docref(NULL TSRMLS_CC, E_NOTICE, "\"%s\" returned as member variable from __sleep() but does not exist", Z_STRVAL_P(d));
						if (igbinary_serialize_string(igsd, Z_STRVAL_P(d), Z_STRLEN_P(d) TSRMLS_CC) != 0) {
							return 1;
						}

						if (igbinary_serialize_null(igsd TSRMLS_CC) != 0) {
							return 1;
						}

						break;
					}

					if (Z_TYPE_P(v) == IS_INDIRECT) {
						v = Z_INDIRECT_P(v);
					}

					if (igbinary_serialize_string(igsd, ZSTR_VAL(mangled_prop_name), ZSTR_LEN(mangled_prop_name) TSRMLS_CC) != 0) {
						zend_string_release(mangled_prop_name);
						return 1;
					}

					zend_string_release(mangled_prop_name);
					if (igbinary_serialize_zval(igsd, v TSRMLS_CC) != 0) {
						return 1;
					}
				} while (0);

			} else {
				/* if all else fails, just serialize the value in anyway. */
				if (igbinary_serialize_string(igsd, Z_STRVAL_P(d), Z_STRLEN_P(d) TSRMLS_CC) != 0) {
					return 1;
				}

				if (Z_TYPE_P(v) == IS_INDIRECT) {
					v = Z_INDIRECT_P(v);
				}

				if (igbinary_serialize_zval(igsd, v TSRMLS_CC) != 0) {
					return 1;
				}
			}
		}
	} ZEND_HASH_FOREACH_END();

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
	PHP_CLASS_ATTRIBUTES;

	zend_class_entry *ce;

	zval f;
	zval h;

	int r = 0;

	unsigned char *serialized_data = NULL;
	size_t serialized_len;


	if (igbinary_serialize_array_ref(igsd, z, true TSRMLS_CC) == 0) {
		return 0;
	}

	ce = Z_OBJCE_P(z);

	/* custom serializer */
	if (ce && ce->serialize != NULL) {
		if (ce->serialize(z, &serialized_data, &serialized_len, (zend_serialize_data *)NULL TSRMLS_CC) == SUCCESS && !EG(exception)) {
			if (igbinary_serialize_object_name(igsd, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name) TSRMLS_CC) != 0) {
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
	if (igbinary_serialize_object_name(igsd, ZSTR_VAL(class_name), ZSTR_LEN(class_name) TSRMLS_CC) != 0) {
		PHP_CLEANUP_CLASS_ATTRIBUTES();
		return 1;
	}
	PHP_CLEANUP_CLASS_ATTRIBUTES();

	if (ce && ce != PHP_IC_ENTRY && zend_hash_str_exists(&ce->function_table, "__sleep", sizeof("__sleep") - 1)) {
		/* function name string */
		ZVAL_STRINGL(&f, "__sleep", sizeof("__sleep") - 1);

		ZVAL_UNDEF(&h);
		/* calling z->__sleep */
		r = call_user_function_ex(CG(function_table), z, &f, &h, 0, 0, 1, NULL TSRMLS_CC);

		zval_dtor(&f);

		if (r == SUCCESS && !EG(exception)) {
			r = 0;

			if (Z_TYPE(h) == IS_UNDEF) {
				/* FIXME: is this ok? */
				/* Valid, but skip */
			} else if (HASH_OF(&h)) {
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
		} else {
			r = 1;
		}

		/* cleanup */
		zval_ptr_dtor(&h);

		return r;
	} else {
		return igbinary_serialize_array(igsd, z, true, incomplete_class TSRMLS_CC);
	}
}
/* }}} */
/* {{{ igbinary_serialize_zval */
/** Serialize zval. */
static int igbinary_serialize_zval(struct igbinary_serialize_data *igsd, zval *z TSRMLS_DC) {
	if (Z_ISREF_P(z)) {
		if (igbinary_serialize8(igsd, (uint8_t) igbinary_type_ref TSRMLS_CC) != 0) {
			return 1;
		}

		switch (Z_TYPE_P(Z_REFVAL_P(z))) {
		case IS_ARRAY:
			return igbinary_serialize_array(igsd, z, false, false TSRMLS_CC);
		case IS_OBJECT:
			break; /* Fall through */
		default:
			/* Serialize a reference if zval already added */
			if (igbinary_serialize_array_ref(igsd, z, false TSRMLS_CC) == 0) {
				return 0;
			}
			/* Fall through */
		}

		ZVAL_DEREF(z);
	}
	switch (Z_TYPE_P(z)) {
		case IS_RESOURCE:
			return igbinary_serialize_null(igsd TSRMLS_CC);
		case IS_OBJECT:
			return igbinary_serialize_object(igsd, z TSRMLS_CC);
		case IS_ARRAY:
			/* if is_ref, then php5 would have called igbinary_serialize_array_ref */
			return igbinary_serialize_array(igsd, z, false, false TSRMLS_CC);
		case IS_STRING:
			return igbinary_serialize_string(igsd, Z_STRVAL_P(z), Z_STRLEN_P(z) TSRMLS_CC);
		case IS_LONG:
			return igbinary_serialize_long(igsd, Z_LVAL_P(z) TSRMLS_CC);
		case IS_NULL:
			return igbinary_serialize_null(igsd TSRMLS_CC);
		case IS_TRUE:
			return igbinary_serialize_bool(igsd, 1 TSRMLS_CC);
		case IS_FALSE:
			return igbinary_serialize_bool(igsd, 0 TSRMLS_CC);
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

	igsd->references = emalloc(sizeof(igsd->references[0]) * igsd->references_capacity);
	if (igsd->references == NULL) {
		return 1;
	}

	igsd->strings = (struct igbinary_unserialize_string_pair *) emalloc(sizeof(struct igbinary_unserialize_string_pair) * igsd->strings_capacity);
	if (igsd->strings == NULL) {
		efree(igsd->references);
		igsd->references = NULL;
		return 1;
	}

	/** Don't bother allocating zvals which __wakeup, probably not common */
	igsd->wakeup = NULL;
	igsd->wakeup_count = 0;
	igsd->wakeup_capacity = 0;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_data_deinit */
/** Deinits igbinary_unserialize_data_init. */
inline static void igbinary_unserialize_data_deinit(struct igbinary_unserialize_data *igsd TSRMLS_DC) {
	if (igsd->strings) {
		efree(igsd->strings);
		igsd->strings = NULL;
	}

	if (igsd->references) {
		efree(igsd->references);
		igsd->references = NULL;
	}
	if (igsd->wakeup) {
		size_t i;
		size_t n = igsd->wakeup_count;
		for (i = 0; i < n; i++) {
			convert_to_null(&igsd->wakeup[i]);
		}
		efree(igsd->wakeup);
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
/** Unserializes zend_long */
inline static int igbinary_unserialize_long(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zend_long *ret TSRMLS_DC) {
	uint32_t tmp32;
#if SIZEOF_ZEND_LONG == 8
	uint64_t tmp64;
#endif

	if (t == igbinary_type_long8p || t == igbinary_type_long8n) {
		if (igsd->buffer_offset + 1 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		*ret = (zend_long) (t == igbinary_type_long8n ? -1 : 1) * igbinary_unserialize8(igsd TSRMLS_CC);
	} else if (t == igbinary_type_long16p || t == igbinary_type_long16n) {
		if (igsd->buffer_offset + 2 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		*ret = (zend_long) (t == igbinary_type_long16n ? -1 : 1) * igbinary_unserialize16(igsd TSRMLS_CC);
	} else if (t == igbinary_type_long32p || t == igbinary_type_long32n) {
		if (igsd->buffer_offset + 4 > igsd->buffer_size) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		/* check for boundaries */
		tmp32 = igbinary_unserialize32(igsd TSRMLS_CC);
#if SIZEOF_ZEND_LONG == 4
		if (tmp32 > 0x80000000 || (tmp32 == 0x80000000 && t == igbinary_type_long32p)) {
			zend_error(E_WARNING, "igbinary_unserialize_long: 64bit long on 32bit platform?");
			tmp32 = 0; /* t == igbinary_type_long32p ? LONG_MAX : LONG_MIN; */
		}
#endif
		*ret = (zend_long) (t == igbinary_type_long32n ? -1 : 1) * tmp32;
	} else if (t == igbinary_type_long64p || t == igbinary_type_long64n) {
#if SIZEOF_ZEND_LONG == 8
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

		*ret = (zend_long) (t == igbinary_type_long64n ? -1 : 1) * tmp64;
#elif SIZEOF_ZEND_LONG == 4
		/* can't put 64bit long into 32bit one, placeholder zero */
		*ret = 0;
		igbinary_unserialize64(igsd TSRMLS_CC);
		zend_error(E_WARNING, "igbinary_unserialize_long: 64bit long on 32bit platform");
#else
#error "Strange sizeof(zend_long)."
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
inline static int igbinary_unserialize_array(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags TSRMLS_DC) {
	/* WANT_OBJECT means that z will be an object (if dereferenced) */
	/* WANT_REF means that z will be wrapped by an IS_REFERENCE */
	size_t n;
	size_t i;

	zval v;
	zval *vp;
	zval *z_deref;

	char *key;
	size_t key_len = 0;
	zend_long key_index = 0;

	enum igbinary_type key_type;

	HashTable *h;

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

	/* n cannot be larger than the number of minimum "objects" in the array */
	if (n > igsd->buffer_size - igsd->buffer_offset) {
		zend_error(E_WARNING, "%s: data size %zu smaller that requested array length %zu.", "igbinary_unserialize_array", igsd->buffer_size - igsd->buffer_offset, n);
		return 1;
	}

	z_deref = z;
	if (flags & WANT_REF) {
		if (!Z_ISREF_P(z)) {
			ZVAL_NEW_REF(z, z);
			z_deref = Z_REFVAL_P(z);
		}
	}
	if ((flags & WANT_OBJECT) == 0) {
		array_init_size(z_deref, n);
		/* add the new array to the list of unserialized references */
		if (igsd_append_ref(igsd, z) == SIZE_MAX) {
			return 1;
		}
	}

	/* empty array */
	if (n == 0) {
		return 0;
	}

	h = HASH_OF(z_deref);
	if ((flags & WANT_OBJECT) != 0) {
		/* Copied from var_unserializer.re. Need to ensure that IGB_REF_VAL doesn't point to invalid data. */
		/* Worst case: All n of the added properties are dynamic. */
		zend_hash_extend(h, zend_hash_num_elements(h) + n, (h->u.flags & HASH_FLAG_PACKED));
	}
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
					ZVAL_UNDEF(z);
					return 1;
				}
				break;
			case igbinary_type_string_id8:
			case igbinary_type_string_id16:
			case igbinary_type_string_id32:
				if (igbinary_unserialize_string(igsd, key_type, &key, &key_len TSRMLS_CC)) {
					zval_dtor(z);
					ZVAL_UNDEF(z);
					return 1;
				}
				break;
			case igbinary_type_string8:
			case igbinary_type_string16:
			case igbinary_type_string32:
				if (igbinary_unserialize_chararray(igsd, key_type, &key, &key_len TSRMLS_CC)) {
					zval_dtor(z);
					ZVAL_UNDEF(z);
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
				ZVAL_UNDEF(z);
				return 1;
		}


		/* first add key into array so refereces can properly and not stack allocated zvals */
		/* Use NULL because inserting UNDEf into array does not add a new element */
		ZVAL_NULL(&v);
		if (key) {
			zend_string *key_str = zend_string_init(key, key_len, 0);

			if ((flags & WANT_OBJECT) != 0) {
				zval *prototype_value = zend_hash_find(h, key_str);
				if (prototype_value != NULL) {
					if (Z_TYPE_P(prototype_value) == IS_INDIRECT) {
						prototype_value = Z_INDIRECT_P(prototype_value);
					}
					convert_to_null(prototype_value);
				}

				zend_hash_update_ind(h, key_str, &v);
			} else {
				zend_hash_update(h, key_str, &v);
			}

			vp = zend_hash_find(h, key_str);
			zend_string_release(key_str);
		} else {
			zend_hash_index_update(h, key_index, &v);
			vp = zend_hash_index_find(h, key_index);
		}

		ZEND_ASSERT(vp != NULL);
		if (Z_TYPE_P(vp) == IS_INDIRECT) {
			vp = Z_INDIRECT_P(vp);
		}

		ZEND_ASSERT(vp != NULL);
		if (igbinary_unserialize_zval(igsd, vp, WANT_CLEAR TSRMLS_CC)) {
			/* zval_ptr_dtor(z); */
			/* zval_ptr_dtor(vp); */
			return 1;
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_object_ser */
/** Unserializes object's property array of objects implementing Serializable -interface. */
inline static int igbinary_unserialize_object_ser(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, zend_class_entry *ce TSRMLS_DC) {
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
inline static int igbinary_unserialize_object(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags TSRMLS_DC) {
	zend_class_entry *ce;

	size_t ref_n;

	zend_string *class_name;
	char *name = NULL;
	size_t name_len = 0;

	int r;

	bool incomplete_class = false;

	zval user_func;
	zval retval;
	zval args[1];

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

	class_name = zend_string_init(name, name_len, 0);

	do {
		/* Try to find class directly */
		if ((ce = zend_lookup_class(class_name TSRMLS_CC)) != NULL) {
			/* FIXME: lookup class may cause exception in load callback */
			break;
		}

		/* Check for unserialize callback */
		if ((PG(unserialize_callback_func) == NULL) || (PG(unserialize_callback_func)[0] == '\0')) {
			incomplete_class = 1;
			ce = PHP_IC_ENTRY;
			break;
		}

		/* Call unserialize callback */
		ZVAL_STRING(&user_func, PG(unserialize_callback_func));
		/* FIXME: Do we need a str copy? */
		/* FIXME: Release arg[0] */
		/* FIXME: Release class_name */
		ZVAL_STR_COPY(&args[0], class_name);
		if (call_user_function_ex(CG(function_table), NULL, &user_func, &retval, 1, args, 0, NULL TSRMLS_CC) != SUCCESS) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "defined (%s) but not found", name);
			incomplete_class = 1;
			ce = PHP_IC_ENTRY;
			zval_ptr_dtor(&args[0]);
			zval_ptr_dtor(&user_func);
			break;
		}
		/* FIXME: always safe? */
		zval_ptr_dtor(&retval);

		/* The callback function may have defined the class */
		ce = zend_lookup_class(class_name TSRMLS_CC);
		if (!ce) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Function %s() hasn't defined the class it was called for", name);
			incomplete_class = true;
			ce = PHP_IC_ENTRY;
		}

		zval_ptr_dtor(&args[0]);
		zval_ptr_dtor(&user_func);
	} while (0);

	zend_string_release(class_name);
	class_name = NULL;

	/* previous user function call may have raised an exception */
	if (EG(exception)) {
		return 1;
	}

	/* add this to the list of unserialized references, get the index */
	ref_n = igsd_append_ref(igsd, z);
	if (ref_n == SIZE_MAX) {
		return 1;
	}

	if (igsd->buffer_offset + 1 > igsd->buffer_size) {
		zend_error(E_WARNING, "igbinary_unserialize_object: end-of-data");
		return 1;
	}
	t = (enum igbinary_type) igbinary_unserialize8(igsd TSRMLS_CC);
	switch (t) {
		case igbinary_type_array8:
		case igbinary_type_array16:
		case igbinary_type_array32:
			if (object_init_ex(IGB_REF_VAL(igsd, ref_n), ce) != SUCCESS) {
				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "igbinary unable to create object for class entry");
				r = 1;
				break;
			}
			/* TODO: This should be dereferenced if necessary */
			if (incomplete_class) {
				php_store_class_name(IGB_REF_VAL(igsd, ref_n), name, name_len);
			}
			r = igbinary_unserialize_array(igsd, t, IGB_REF_VAL(igsd, ref_n), flags|WANT_OBJECT TSRMLS_CC);
			break;
		case igbinary_type_object_ser8:
		case igbinary_type_object_ser16:
		case igbinary_type_object_ser32:

			r = igbinary_unserialize_object_ser(igsd, t, IGB_REF_VAL(igsd, ref_n), ce TSRMLS_CC);
            if (r != 0) {
                break;
            }
			if (incomplete_class) {
				php_store_class_name(IGB_REF_VAL(igsd, ref_n), name, name_len);
			}
			if ((flags & WANT_REF) != 0) {
				ZVAL_MAKE_REF(z);
			}
			break;
		default:
			zend_error(E_WARNING, "igbinary_unserialize_object: unknown object inner type '%02x', position %zu", t, igsd->buffer_offset);
			r = 1;
	}

	/* If unserialize was successful, call __wakeup if __wakeup exists for this object. */
	if (r == 0) {
		zval *ztemp = IGB_REF_VAL(igsd, ref_n);
		zend_class_entry *ztemp_ce;
		/* May have created a reference while deserializing an object, if it was recursive. */
		ZVAL_DEREF(ztemp);
		if (Z_TYPE_P(ztemp) != IS_OBJECT) {
			zend_error(E_WARNING, "igbinary_unserialize_object preparing to __wakeup: created non-object somehow?", t, igsd->buffer_offset);
			return 1;
		}
		ztemp_ce = Z_OBJCE_P(ztemp);
		if (ztemp_ce != PHP_IC_ENTRY &&
			zend_hash_str_exists(&ztemp_ce->function_table, "__wakeup", sizeof("__wakeup") - 1)) {
			if (igsd_defer_wakeup(igsd, ztemp)) {
				r = 1;
			}
		}
	}

	/* ZVAL_COPY_VALUE(z, IGB_REF_VAL(igsd, ref_n)); */

	return r;
}
/* }}} */
/* {{{ igbinary_unserialize_ref */
/** Unserializes array or object by reference. */
inline static int igbinary_unserialize_ref(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags TSRMLS_DC) {
	size_t n;
	zval* z_ref = NULL;

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
		zend_error(E_WARNING, "igbinary_unserialize_ref: invalid reference %zu >= %zu", (int) n, (int)igsd->references_count);
		return 1;
	}

	if (z != NULL) {
		/* FIXME: check with is refcountable or some such */
		zval_ptr_dtor(z);
		ZVAL_UNDEF(z);
	}

	z_ref = IGB_REF_VAL(igsd, n);

	/**
	 * Permanently convert the zval in IGB_REF_VAL() into a IS_REFERENCE if it wasn't already one.
	 * TODO: Can there properly be multiple reference groups to an object?
	 * Similar to https://github.com/php/php-src/blob/master/ext/standard/var_unserializer.re , for "R:"
	 * Using `flags` because igbinary_unserialize_ref might be used both for copy on writes ($a = $b = [2]) and by PHP references($a = &$b).
	 */
	if ((flags & WANT_REF) != 0) {
		/* Want to create an IS_REFERENCE, not just to share the same value until modified. */
        ZVAL_COPY(z, z_ref);
        if (!Z_ISREF_P(z)) {
            ZVAL_MAKE_REF(z); /* Convert original zval data to a reference and replace the entry in IGB_REF_VAL with that. */
            IGB_REF_VAL(igsd, n) = z;
        }
	} else {
		ZVAL_DEREF(z_ref);
		ZVAL_COPY(z, z_ref);
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_zval */
/** Unserialize zval. */
static int igbinary_unserialize_zval(struct igbinary_unserialize_data *igsd, zval *const z, int flags TSRMLS_DC) {
	enum igbinary_type t;

	zend_long tmp_long;
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
			if (igbinary_unserialize_zval(igsd, z, WANT_REF TSRMLS_CC)) {
				return 1;
			}

			/* If it is already a ref, nothing to do */
			if (Z_ISREF_P(z)) {
				break;
			}

			switch (Z_TYPE_P(z)) {
				case IS_STRING:
				case IS_LONG:
				case IS_NULL:
				case IS_DOUBLE:
				case IS_FALSE:
				case IS_TRUE:
					/* add the unserialized scalar to the list of unserialized references. Objects and arrays were already added in igbinary_unserialize_zval. */
					if (igsd_append_ref(igsd, z) == SIZE_MAX) {
						return 1;
					}
					break;
				default:
					break;
			}
			/* Permanently convert the zval in IGB_REF_VAL() into a IS_REFERENCE if it wasn't already one. */
			/* TODO: Support multiple reference groups to the same object */
			/* Similar to https://github.com/php/php-src/blob/master/ext/standard/var_unserializer.re , for "R:" */
			ZVAL_MAKE_REF(z);

			break;
		case igbinary_type_objref8:
		case igbinary_type_objref16:
		case igbinary_type_objref32:
		case igbinary_type_ref8:
		case igbinary_type_ref16:
		case igbinary_type_ref32:
			if (igbinary_unserialize_ref(igsd, t, z, flags TSRMLS_CC)) {
				return 1;
			}
			break;
		case igbinary_type_object8:
		case igbinary_type_object16:
		case igbinary_type_object32:
		case igbinary_type_object_id8:
		case igbinary_type_object_id16:
		case igbinary_type_object_id32:
			if (igbinary_unserialize_object(igsd, t, z, flags TSRMLS_CC)) {
				return 1;
			}
			break;
		case igbinary_type_array8:
		case igbinary_type_array16:
		case igbinary_type_array32:
			if (igbinary_unserialize_array(igsd, t, z, flags TSRMLS_CC)) {
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
