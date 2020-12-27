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

#if HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION)
# include "ext/session/php_session.h"
#endif

#include "ext/standard/php_incomplete_class.h"

#if PHP_VERSION_ID < 70400
#define zend_get_properties_for(struc, purpose) Z_OBJPROP_P((struc))

#define zend_release_properties(ht) do {} while (0)
#endif

#if PHP_VERSION_ID < 70300
#define zend_string_efree(s) zend_string_release((s))
#define GC_ADDREF(p) (++GC_REFCOUNT((p)))
#endif

#if defined(HAVE_APCU_SUPPORT)
# include "ext/apcu/apc_serializer.h"
#endif /* HAVE_APCU_SUPPORT */

#include "php_igbinary.h"

#include "igbinary.h"
#include "igbinary_macros.h"

#include <assert.h>
#include <ctype.h>

#ifndef PHP_WIN32
# include <inttypes.h>
# include <stdbool.h>
# include <stdint.h>
#endif


#include <stddef.h>
#include "hash.h"
#include "hash_ptr.h"
#include "zend_alloc.h"

#if HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION)
/** Session serializer function prototypes. */
PS_SERIALIZER_FUNCS(igbinary);
#endif /* HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION) */

#if defined(HAVE_APCU_SUPPORT)
/** Apc serializer function prototypes */
static int APC_SERIALIZER_NAME(igbinary) (APC_SERIALIZER_ARGS);
static int APC_UNSERIALIZER_NAME(igbinary) (APC_UNSERIALIZER_ARGS);
#endif

#if PHP_VERSION_ID < 70300
#define zend_string_release_ex(s, persistent) zend_string_release((s))
#endif

#define RETURN_1_IF_NON_ZERO(cmd) \
  if (UNEXPECTED((cmd) != 0)) {   \
    return 1;                     \
  }

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

/* Defers calls to zval_ptr_dtor for values that are refcounted. */
struct deferred_dtor_tracker {
	zval *zvals;           /**< refcounted objects and arrays to call dtor on after unserializing. See i_zval_ptr_dtor */
	size_t count;    /**< count of refcounted in array for calls to dtor */
	size_t capacity; /**< capacity of refcounted in array for calls to dtor */
};

/** Serializer data.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 */
struct igbinary_serialize_data {
	uint8_t *buffer;               /**< Buffer. */
	size_t buffer_size;            /**< Buffer size. */
	size_t buffer_capacity;        /**< Buffer capacity. */
	bool scalar;                   /**< Serializing scalar. */
	bool compact_strings;          /**< Check for duplicate strings. */
	struct hash_si strings;        /**< Hash of already serialized strings. */
	struct hash_si_ptr references; /**< Hash of already serialized potential references. (non-NULL uintptr_t => int32_t) */
	int references_id;             /**< Number of things that the unserializer might think are references. >= length of references */
	int string_count;              /**< Serialized string count, used for back referencing */

	struct deferred_dtor_tracker deferred_dtor_tracker;  /**< refcounted objects and arrays to call dtor on after serializing. See i_zval_ptr_dtor */
};

/*
Object {
   reference {scalar, object, array, null} (convert to reference, share reference in zval_ref)
   object {} (convert to zend_object, share zend_object* in reference)
   array {} (convert to zend_array, share zend_array* in reference)
   empty array {} (use ZVAL_EMPTY_ARRAY to create zvals)
}
*/
enum zval_ref_type {
	IG_REF_IS_REFERENCE,   // Points to zend_reference
	IG_REF_IS_OBJECT,      // Points to zend_object
	IG_REF_IS_ARRAY,       // Points to zend_array
#if PHP_VERSION_ID >= 70300
	IG_REF_IS_EMPTY_ARRAY, // Use the macro ZVAL_EMPTY_ARRAY to create a pointer to the empty array with the correct type info flags.
#endif
};

struct igbinary_value_ref {
	// We reuse temporary values for object properties that are references or arrays.
	union {
		zend_reference *reference;
		zend_object *object;
		zend_array *array;
	} reference;
	enum zval_ref_type type;
};

struct deferred_unserialize_call {
	zval param;          /* The array parameter passed to the __unserialize call */
	zend_object *object; /* The object which has a deferred call to __unserialize that is going to get called. */
};

struct deferred_call {
	union {
		zend_object *wakeup;
#if PHP_VERSION_ID >= 70400
		/* Currently, zvals are safe to relocate */
		struct deferred_unserialize_call unserialize;
#endif
	} data;
#if PHP_VERSION_ID >= 70400
	zend_bool is_unserialize;
#endif
};
/** Unserializer data.
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 */
struct igbinary_unserialize_data {
	const uint8_t *buffer;          /**< Buffer with bytes to unserialize. */
	const uint8_t *buffer_end;      /**< Buffer size. */
	const uint8_t *buffer_ptr;      /**< Current read offset. */

	zend_string **strings;          /**< Unserialized strings. */
	size_t strings_count;           /**< Unserialized string count. */
	size_t strings_capacity;        /**< Unserialized string array capacity. */

	struct igbinary_value_ref *references; /**< Unserialized Arrays/Objects/References */
	size_t references_count;        /**< Unserialized array/objects count. */
	size_t references_capacity;     /**< Unserialized array/object array capacity. */

	struct deferred_call *deferred; /**< objects&data for calls to __unserialize/__wakeup */
	size_t deferred_count;          /**< count of objects in array for calls to __unserialize/__wakeup */
	size_t deferred_capacity;       /**< capacity of objects in array for calls to __unserialize/__wakeup */
	zend_bool deferred_finished;    /**< whether the deferred calls were performed */
	struct deferred_dtor_tracker deferred_dtor_tracker;  /**< refcounted objects and arrays to call dtor on after unserializing. See i_zval_ptr_dtor */
};

#define IGB_REF_VAL_2(igsd, n)	((igsd)->references[(n)])
#define IGB_NEEDS_MORE_DATA(igsd, n)	UNEXPECTED((size_t)((igsd)->buffer_end - (igsd)->buffer_ptr) < (n))
#define IGB_REMAINING_BYTES(igsd)	((unsigned int)((igsd)->buffer_end - (igsd)->buffer_ptr))
#define IGB_BUFFER_OFFSET(igsd)	((unsigned int)((igsd)->buffer_ptr - (igsd)->buffer))

#define WANT_CLEAR (0)
#define WANT_REF   (1 << 1)

/* }}} */
/* {{{ Serializing functions prototypes */
inline static int igbinary_serialize_data_init(struct igbinary_serialize_data *igsd, bool scalar);
inline static void igbinary_serialize_data_deinit(struct igbinary_serialize_data *igsd, int free_buffer);

inline static void igbinary_serialize_header(struct igbinary_serialize_data *igsd);

inline static int igbinary_serialize8(struct igbinary_serialize_data *igsd, uint8_t i);
inline static int igbinary_serialize16(struct igbinary_serialize_data *igsd, uint16_t i);
inline static int igbinary_serialize32(struct igbinary_serialize_data *igsd, uint32_t i);
inline static int igbinary_serialize64(struct igbinary_serialize_data *igsd, uint64_t i);

inline static int igbinary_serialize_null(struct igbinary_serialize_data *igsd);
inline static int igbinary_serialize_bool(struct igbinary_serialize_data *igsd, int b);
inline static int igbinary_serialize_long(struct igbinary_serialize_data *igsd, zend_long l);
inline static int igbinary_serialize_double(struct igbinary_serialize_data *igsd, double d);
inline static int igbinary_serialize_string(struct igbinary_serialize_data *igsd, zend_string *s);
inline static int igbinary_serialize_chararray(struct igbinary_serialize_data *igsd, const char *s, size_t len);

inline static int igbinary_serialize_array(struct igbinary_serialize_data *igsd, zval *z, bool object, bool incomplete_class, bool serialize_props);
inline static int igbinary_serialize_array_ref(struct igbinary_serialize_data *igsd, zval *z, bool object);
inline static int igbinary_serialize_array_sleep(struct igbinary_serialize_data *igsd, zval *z, HashTable *ht, zend_class_entry *ce);
inline static int igbinary_serialize_object_name(struct igbinary_serialize_data *igsd, zend_string *name);
inline static int igbinary_serialize_object(struct igbinary_serialize_data *igsd, zval *z);

static int igbinary_serialize_zval(struct igbinary_serialize_data *igsd, zval *z);
/* }}} */
/* {{{ Unserializing functions prototypes */
inline static int igbinary_unserialize_data_init(struct igbinary_unserialize_data *igsd);
inline static void igbinary_unserialize_data_deinit(struct igbinary_unserialize_data *igsd);

inline static int igbinary_unserialize_header(struct igbinary_unserialize_data *igsd);

inline static uint8_t igbinary_unserialize8(struct igbinary_unserialize_data *igsd);
inline static uint16_t igbinary_unserialize16(struct igbinary_unserialize_data *igsd);
inline static uint32_t igbinary_unserialize32(struct igbinary_unserialize_data *igsd);
inline static uint64_t igbinary_unserialize64(struct igbinary_unserialize_data *igsd);

inline static int igbinary_unserialize_long(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zend_long *ret);
inline static int igbinary_unserialize_double(struct igbinary_unserialize_data *igsd, enum igbinary_type t, double *ret);
inline static zend_string *igbinary_unserialize_string(struct igbinary_unserialize_data *igsd, enum igbinary_type t);
inline static zend_string *igbinary_unserialize_chararray(struct igbinary_unserialize_data *igsd, enum igbinary_type t);

inline static int igbinary_unserialize_array(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags, zend_bool create_ref);
inline static int igbinary_unserialize_object(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags);
inline static int igbinary_unserialize_object_ser(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, zend_class_entry *ce);
inline static int igbinary_unserialize_ref(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags);

inline static int igbinary_unserialize_zval(struct igbinary_unserialize_data *igsd, zval *const z, int flags);
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
static const zend_module_dep igbinary_module_deps[] = {
	ZEND_MOD_REQUIRED("standard")
#if HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION)
	ZEND_MOD_REQUIRED("session")
#endif
#if defined(HAVE_APCU_SUPPORT)
	ZEND_MOD_OPTIONAL("apcu")
#endif
	ZEND_MOD_END
};
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
/**
 * The module init function.
 * This allocates the persistent resources of this PHP module.
 */
PHP_MINIT_FUNCTION(igbinary) {
	(void)type;
	(void)module_number;
	ZEND_INIT_MODULE_GLOBALS(igbinary, php_igbinary_init_globals, NULL);

#if HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION)
	php_session_register_serializer("igbinary",
		PS_SERIALIZER_ENCODE_NAME(igbinary),
		PS_SERIALIZER_DECODE_NAME(igbinary));
#endif

#if defined(HAVE_APCU_SUPPORT)
	apc_register_serializer("igbinary",
		APC_SERIALIZER_NAME(igbinary),
		APC_UNSERIALIZER_NAME(igbinary),
		NULL);
#endif

	REGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */
/* {{{ PHP_MSHUTDOWN_FUNCTION */
/**
 * The module shutdown function.
 * This cleans up all persistent resources of this PHP module.
 */
PHP_MSHUTDOWN_FUNCTION(igbinary) {
	(void)type;
	(void)module_number;

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
	(void)zend_module;
	php_info_print_table_start();
	php_info_print_table_row(2, "igbinary support", "enabled");
	php_info_print_table_row(2, "igbinary version", PHP_IGBINARY_VERSION);
#if defined(HAVE_APCU_SUPPORT)
	php_info_print_table_row(2, "igbinary APCu serializer ABI", APC_SERIALIZER_ABI);
#else
	php_info_print_table_row(2, "igbinary APCu serializer ABI", "no");
#endif
#if HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION)
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
static inline size_t igsd_append_ref(struct igbinary_unserialize_data *igsd, struct igbinary_value_ref v)
{
	size_t ref_n;
	if (igsd->references_count + 1 >= igsd->references_capacity) {
		while (igsd->references_count + 1 >= igsd->references_capacity) {
			igsd->references_capacity *= 2;
		}

		struct igbinary_value_ref *new_references = erealloc(igsd->references, sizeof(igsd->references[0]) * igsd->references_capacity);
		if (UNEXPECTED(new_references == NULL)) {
			return SIZE_MAX;
		}
		igsd->references = new_references;
	}

	ref_n = igsd->references_count++;
	IGB_REF_VAL_2(igsd, ref_n) = v;
	return ref_n;
}

static inline int igsd_ensure_defer_capacity(struct igbinary_unserialize_data *igsd) {
	if (igsd->deferred_count >= igsd->deferred_capacity) {
		if (igsd->deferred_capacity == 0) {
			igsd->deferred_capacity = 2;
			igsd->deferred = emalloc(sizeof(igsd->deferred[0]) * igsd->deferred_capacity);
		} else {
			igsd->deferred_capacity *= 2;
			struct deferred_call *old_deferred = igsd->deferred;
			igsd->deferred = erealloc(old_deferred, sizeof(igsd->deferred[0]) * igsd->deferred_capacity);
			if (UNEXPECTED(igsd->deferred == NULL)) {
				igsd->deferred = old_deferred;
				return 1;
			}
		}
	}
	return 0;
}

static inline int igsd_defer_wakeup(struct igbinary_unserialize_data *igsd, zend_object *object) {
	// TODO: This won't be properly garbage collected if there is an OOM error, but would php terminate instead?
	RETURN_1_IF_NON_ZERO(igsd_ensure_defer_capacity(igsd));

	struct deferred_call *c = &igsd->deferred[igsd->deferred_count++];
	c->data.wakeup = object;
#if PHP_VERSION_ID >= 70400
	c->is_unserialize = 0;
#endif
	return 0;
}

/* igsd_defer_unserialize {{{ */
#if PHP_VERSION_ID >= 70400
static inline int igsd_defer_unserialize(struct igbinary_unserialize_data *igsd, zend_object *object, zval param) {
	RETURN_1_IF_NON_ZERO(igsd_ensure_defer_capacity(igsd));

	struct deferred_call *c = &igsd->deferred[igsd->deferred_count++];
	struct deferred_unserialize_call* call = &c->data.unserialize;
	call->object = object;
	ZEND_ASSERT(Z_TYPE(param) == IS_ARRAY);
	call->param = param;
	c->is_unserialize = 1;
	return 0;
}
#endif
/* }}} */

/* {{{ igbinary_finish_deferred_calls
 * After all object instances were unserialized, perform the deferred calls to __wakeup() on all of the objects implementing that method.
 */
static int igbinary_finish_deferred_calls(struct igbinary_unserialize_data *igsd) {
#if PHP_VERSION_ID >= 70400 && PHP_VERSION_ID < 80000
	zval unserialize_name;
#endif
	zval wakeup_name;
	size_t i;
	zend_bool delayed_call_failed = 0;
	igsd->deferred_finished = 1;
	if (igsd->deferred_count == 0) { /* nothing to do */
		return 0;
	}
#if PHP_VERSION_ID >= 70400 && PHP_VERSION_ID < 80000
	ZVAL_STRINGL(&unserialize_name, "__unserialize", sizeof("__unserialize") - 1);
#endif
	ZVAL_STRINGL(&wakeup_name, "__wakeup", sizeof("__wakeup") - 1);
	for (i = 0; i < igsd->deferred_count; i++) {
		struct deferred_call *deferred = &igsd->deferred[i];
#if PHP_VERSION_ID >= 70400
		if (deferred->is_unserialize) {
			struct deferred_unserialize_call *unserialize_call = &deferred->data.unserialize;
			zend_object *const obj = unserialize_call->object;
			ZEND_ASSERT(Z_TYPE(unserialize_call->param) == IS_ARRAY);

			if (!delayed_call_failed) {
#if PHP_VERSION_ID >= 80000
				/* Copy the parameter for __unserialize so that changes in __unserialize won't mutate the original. */
				// ZVAL_COPY(&param, &unserialize_call->param);
				BG(serialize_lock)++;
				zend_call_known_instance_method_with_1_params(
					obj->ce->__unserialize, obj, NULL, &unserialize_call->param);
				if (EG(exception)) {
					delayed_call_failed = 1;
					GC_ADD_FLAGS(obj, IS_OBJ_DESTRUCTOR_CALLED);
				}
				BG(serialize_lock)--;
#else
				zval retval;
				zval zv;
				ZVAL_OBJ(&zv, obj);
				/* Copy the parameter for __unserialize so that changes in __unserialize won't mutate the original. */
				// ZVAL_COPY(&param, &unserialize_call->param);
				BG(serialize_lock)++;
				if (call_user_function(CG(function_table), &zv, &unserialize_name, &retval, 1, &unserialize_call->param) == FAILURE || Z_ISUNDEF(retval))
				{
					delayed_call_failed = 1;
					GC_ADD_FLAGS(obj, IS_OBJ_DESTRUCTOR_CALLED);
				}
				BG(serialize_lock)--;
				zval_ptr_dtor(&retval);
#endif
			} else {
				GC_ADD_FLAGS(obj, IS_OBJ_DESTRUCTOR_CALLED);
			}

			zval_ptr_dtor(&unserialize_call->param);
		} else
#endif
		{
			zend_object *obj = deferred->data.wakeup;
			if (!delayed_call_failed) {
				zval retval; /* return value of __wakeup */
				zval rval;
				ZVAL_OBJ(&rval, obj);
				if (UNEXPECTED(call_user_function(CG(function_table), &rval, &wakeup_name, &retval, 0, 0) == FAILURE || Z_ISUNDEF(retval))) {
					delayed_call_failed = 1;
					GC_ADD_FLAGS(obj, IS_OBJ_DESTRUCTOR_CALLED);
				}
				zval_ptr_dtor(&retval);
			} else {
				GC_ADD_FLAGS(obj, IS_OBJ_DESTRUCTOR_CALLED);
			}
		}
	}
	zval_dtor(&wakeup_name);
#if PHP_VERSION_ID >= 70400 && PHP_VERSION_ID < 80000
	zval_dtor(&unserialize_name);
#endif
	return delayed_call_failed;
}
/* }}} */
/* }}} */

/* {{{ igsd_ensure_deferred_dtor_capacity(struct igbinary_serialize_data *igsd) */
static inline int igsd_ensure_deferred_dtor_capacity(struct deferred_dtor_tracker *tracker) {
	if (tracker->count >= tracker->capacity) {
		if (tracker->capacity == 0) {
			tracker->capacity = 2;
			tracker->zvals = emalloc(sizeof(tracker->zvals[0]) * tracker->capacity);
		} else {
			tracker->capacity *= 2;
			zval *old_deferred_dtor = tracker->zvals;
			tracker->zvals = erealloc(old_deferred_dtor, sizeof(tracker->zvals[0]) * tracker->capacity);
			if (UNEXPECTED(tracker->zvals == NULL)) {
				tracker->zvals = old_deferred_dtor;
				return 1;
			}
		}
	}
	return 0;
}
/* }}} */

/* {{{ free_deferred_dtors(struct deferred_dtor_tracker *tracker) */
static inline void free_deferred_dtors(struct deferred_dtor_tracker *tracker) {
	zval *const zvals = tracker->zvals;
	if (zvals) {
		const size_t n = tracker->count;
		size_t i;
		for (i = 0; i < n; i++) {
			/* fprintf(stderr, "freeing i=%d id=%d refcount=%d\n", (int)i, (int)Z_OBJ_HANDLE(zvals[i]), (int)Z_REFCOUNT(zvals[i])); */
			zval_ptr_dtor(&zvals[i]);
		}
		efree(zvals);
	}
}
/* }}} */

/* {{{ igsd_addref_and_defer_dtor(struct igbinary_serialize_data *igsd, zval *z) */
static inline int igsd_addref_and_defer_dtor(struct deferred_dtor_tracker *tracker, zval *z) {
	if (!Z_REFCOUNTED_P(z)) {
		return 0;
	}
	if (igsd_ensure_deferred_dtor_capacity(tracker)) {
		return 1;
	}

	ZEND_ASSERT(Z_REFCOUNT_P(z) >= 1);  /* Expect that there were references at the time this was serialized */
	ZVAL_COPY(&tracker->zvals[tracker->count++], z);  /* Copy and increase reference count */
	return 0;
}
/* }}} */
/* {{{ igsd_defer_dtor(struct igbinary_serialize_data *igsd, zval *z) */
static inline int igsd_defer_dtor(struct deferred_dtor_tracker *tracker, zval *z) {
	if (!Z_REFCOUNTED_P(z)) {
		return 0;
	}
	if (igsd_ensure_deferred_dtor_capacity(tracker)) {
		return 1;
	}

	ZEND_ASSERT(Z_REFCOUNT_P(z) >= 1);  /* Expect that there were references at the time this was serialized */
	ZVAL_COPY_VALUE(&tracker->zvals[tracker->count++], z);  /* Copy without increasing reference count */
	return 0;
}
/* }}} */
/* {{{ int igbinary_serialize(uint8_t**, size_t*, zval*) */
IGBINARY_API int igbinary_serialize(uint8_t **ret, size_t *ret_len, zval *z) {
	return igbinary_serialize_ex(ret, ret_len, z, NULL);
}
/* }}} */
/* {{{ int igbinary_serialize_ex(uint8_t**, size_t*, zval*, igbinary_memory_manager*) */
/**
 * Serializes data, and writes the allocated byte buffer into ret and the buffer's length into ret_len.
 * @param ret output parameter
 * @param ret_len length of byte buffer ret
 * @param z the zval (data) to serialize
 * @param memory_manager (nullable) the memory manager to use for allocating/reallocating the buffer of serialized data. Used by extensions such as APCu
 * @return 0 on success, 1 on failure
 */
IGBINARY_API int igbinary_serialize_ex(uint8_t **ret, size_t *ret_len, zval *z, struct igbinary_memory_manager *memory_manager) {
	struct igbinary_serialize_data igsd;
	uint8_t *tmpbuf;
	// While we can't get passed references through the PHP_FUNCTIONs igbinary declares, third party code can invoke igbinary's methods with references.
	// See https://github.com/php-memcached-dev/php-memcached/issues/326
	if (UNEXPECTED(Z_TYPE_P(z) == IS_INDIRECT)) {
		z = Z_INDIRECT_P(z);
	}
	ZVAL_DEREF(z);

	if (UNEXPECTED(igbinary_serialize_data_init(&igsd, Z_TYPE_P(z) != IS_OBJECT && Z_TYPE_P(z) != IS_ARRAY))) {
		zend_error(E_WARNING, "igbinary_serialize: cannot init igsd");
		return 1;
	}

	igbinary_serialize_header(&igsd);

	if (UNEXPECTED(igbinary_serialize_zval(&igsd, z) != 0)) {
		igbinary_serialize_data_deinit(&igsd, 1);
		return 1;
	}

	/* Explicit null termination */
	/* TODO: Stop doing this in the next major version, serialized data can contain nulls in the middle and callers should check length  */
	if (UNEXPECTED(igbinary_serialize8(&igsd, 0) != 0)) {
		igbinary_serialize_data_deinit(&igsd, 1);
		return 1;
	}

	/* shrink buffer to the real length, ignore errors */
	if (memory_manager) {
		tmpbuf = memory_manager->alloc(igsd.buffer_size, memory_manager->context);
		if (tmpbuf != NULL) {
			memcpy(tmpbuf, igsd.buffer, igsd.buffer_size);
		}
		/* Unconditionally free the emalloc()ed data */
		igbinary_serialize_data_deinit(&igsd, 1);

		if (tmpbuf == NULL) {
			return 1;
		}
		*ret = tmpbuf;
		*ret_len = igsd.buffer_size - 1;
	} else {
		/* Set return values */
		*ret_len = igsd.buffer_size - 1;
		*ret = igsd.buffer;

		igbinary_serialize_data_deinit(&igsd, 0);
	}

	return 0;
}
/* }}} */
/* {{{ int igbinary_unserialize(const uint8_t *, size_t, zval **) */
/**
 * Unserializes the data into z
 * @param buf the read-only buffer with the serialized data
 * @param buf_len the length of that buffer.
 * @param z output parameter. Will contain the unserialized value(zval).
 * @return 0 on success, 1 on failure
 */
IGBINARY_API int igbinary_unserialize(const uint8_t *buf, size_t buf_len, zval *z) {
	struct igbinary_unserialize_data igsd;

	igbinary_unserialize_data_init(&igsd);

	igsd.buffer = buf;
	igsd.buffer_ptr = buf;
	igsd.buffer_end = buf + buf_len;

	if (igbinary_unserialize_header(&igsd)) {
		igbinary_unserialize_data_deinit(&igsd);
		return 1;
	}

	if (igbinary_unserialize_zval(&igsd, z, WANT_CLEAR)) {
		igbinary_unserialize_data_deinit(&igsd);
		return 1;
	}

	if (igsd.buffer_ptr < igsd.buffer_end) {
		// https://github.com/igbinary/igbinary/issues/64
		zend_error(E_WARNING, "igbinary_unserialize: received more data to unserialize than expected");
		igbinary_unserialize_data_deinit(&igsd);
		return 1;
	}

	if (igbinary_finish_deferred_calls(&igsd)) {
		igbinary_unserialize_data_deinit(&igsd);
		return 1;
	}
	igbinary_unserialize_data_deinit(&igsd);

	return 0;
}
/* }}} */
/* {{{ proto string igbinary_unserialize(mixed value) */
/**
 * @see igbinary.php for more detailed API documentation.
 */
PHP_FUNCTION(igbinary_unserialize) {
	char *string = NULL;
	size_t string_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &string, &string_len) == FAILURE) {
		RETURN_NULL();
	}

	if (string_len <= 0) {
		RETURN_FALSE;
	}

	if (igbinary_unserialize((uint8_t *)string, string_len, return_value) != 0) {
		/* FIXME: is this a good place? a catch all */
		zval_ptr_dtor(return_value);
		RETURN_NULL();
	}
}
/* }}} */
/* {{{ proto mixed igbinary_serialize(string value) */
/**
 * @see igbinary.php for more detailed API documentation.
 */
PHP_FUNCTION(igbinary_serialize) {
	zval *z;
	uint8_t *string;
	size_t string_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &z) == FAILURE) {
		RETURN_NULL();
	}

	if (igbinary_serialize(&string, &string_len, z) != 0) {
		RETURN_NULL();
	}

	RETVAL_STRINGL((char *)string, string_len);
	efree(string);
}
/* }}} */
#if HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION)
/* {{{ Serializer encode function */
/**
 * This provides a serializer encode function for PHP's session module (using igbinary),
 * if igbinary was compiled with session support.
 *
 * Session support has to be statically compiled into php to use igbinary,
 * due to the lack of a cross-platform way to register a session serializer/unserializer
 * when the session module isn't available.
 */
PS_SERIALIZER_ENCODE_FUNC(igbinary)
{
	zval *session_vars;
	zend_string *result;
	struct igbinary_serialize_data igsd;

	session_vars = &(PS(http_session_vars));
	if (Z_ISREF_P(session_vars)) {
		session_vars = Z_REFVAL_P(session_vars);
	}
	if (igbinary_serialize_data_init(&igsd, false)) {
		zend_error(E_WARNING, "igbinary_serialize: cannot init igsd");
		return ZSTR_EMPTY_ALLOC();
	}

	igbinary_serialize_header(&igsd);

	/** We serialize the passed in array of session_var (including the empty array, for #231) */
	/** the same way we would serialize a regular array. */
	/** The corresponding PS_SERIALIZER_DECODE_FUNC will unserialize the array and individually add the session variables. */
	if (igbinary_serialize_array(&igsd, session_vars, false, false, true) != 0) {
		igbinary_serialize_data_deinit(&igsd, 1);
		zend_error(E_WARNING, "igbinary_serialize: cannot serialize session variables");
		return ZSTR_EMPTY_ALLOC();
	}

	/* Copy the buffer to a new zend_string */
	result = zend_string_init((const char *)igsd.buffer, igsd.buffer_size, 0);
	igbinary_serialize_data_deinit(&igsd, 1);

	return result;
}
/* }}} */
/* {{{ Serializer decode function */
/**
 * This provides a serializer decode function for PHP's session module (using igbinary),
 * if igbinary was compiled with session support.
 *
 * Session support has to be statically compiled into php to use igbinary,
 * due to the lack of a cross-platform way to register a session serializer/unserializer
 * when the session module isn't available.
 *
 * This is similar to PS_SERIALIZER_DECODE_FUNC(php) from ext/session/session.c
 */
PS_SERIALIZER_DECODE_FUNC(igbinary) {
	HashTable *tmp_hash;
	zval z;
	zval *d;
	zend_string *key;

	struct igbinary_unserialize_data igsd;

	if (!val || vallen == 0) {
		return SUCCESS;
	}

	if (igbinary_unserialize_data_init(&igsd) != 0) {
		return FAILURE;
	}

	igsd.buffer = (const uint8_t *)val;
	igsd.buffer_ptr = igsd.buffer;
	igsd.buffer_end = igsd.buffer + vallen;

	if (igbinary_unserialize_header(&igsd)) {
		igbinary_unserialize_data_deinit(&igsd);
		return FAILURE;
	}

	/** The serializer serialized the session variables as an array. So, we unserialize that array. */
	/** We then iterate over the array to set the individual session variables (managing the reference counts), then free the original array. */
	if (igbinary_unserialize_zval(&igsd, &z, WANT_CLEAR)) {
		igbinary_unserialize_data_deinit(&igsd);
		return FAILURE;
	}

	if (igbinary_finish_deferred_calls(&igsd)) {
		igbinary_unserialize_data_deinit(&igsd);
		return FAILURE;
	}

	igbinary_unserialize_data_deinit(&igsd);

	/* Validate that this is of the correct data type */
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
		if (php_set_session_var(key, d, NULL)) { /* Added to session successfully */
			/* Refcounted types such as arrays, objects, references need to have references incremented manually, so that zval_ptr_dtor doesn't clean up pointers they include. */
			/* Non-refcounted types have the data copied. */
			Z_TRY_ADDREF_P(d);
		}
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&z);

	return SUCCESS;
}
/* }}} */
#endif /* HAVE_PHP_SESSION && !defined(COMPILE_DL_SESSION) */

#if defined(HAVE_APCU_SUPPORT)
/* {{{ apc_serialize function */
static int APC_SERIALIZER_NAME(igbinary) ( APC_SERIALIZER_ARGS ) {
	(void)config;

	if (igbinary_serialize(buf, buf_len, (zval *)value) == 0) {
		/* flipped semantics - We return 1 to indicate success to APCu (and 0 for failure) */
		return 1;
	}
	return 0;
}
/* }}} */
/* {{{ apc_unserialize function */
static int APC_UNSERIALIZER_NAME(igbinary) ( APC_UNSERIALIZER_ARGS ) {
	(void)config;

	if (igbinary_unserialize(buf, buf_len, value) == 0) {
		/* flipped semantics - We return 1 to indicate success to APCu (and 0 for failure) */
		return 1;
	}
	/* Failed. free return value */
	zval_ptr_dtor(value);
	ZVAL_NULL(value); /* and replace the incomplete value with null just in case APCu uses it in the future */
	return 0;
}
/* }}} */
#endif

/* {{{ igbinary_serialize_data_init */
/**
 * Allocates data structures needed by igbinary_serialize_data,
 * and sets properties of igsd to their defaults.
 * @param igsd the struct to initialize
 * @param scalar true if the data being serialized is a scalar
 * @param memory_manager optional override of the memory manager
 */
inline static int igbinary_serialize_data_init(struct igbinary_serialize_data *igsd, bool scalar) {
	int r = 0;

	igsd->buffer = NULL;
	igsd->buffer_size = 0;
	igsd->buffer_capacity = 32;
	igsd->string_count = 0;

	igsd->buffer = emalloc(igsd->buffer_capacity);
	if (UNEXPECTED(igsd->buffer == NULL)) {
		return 1;
	}

	igsd->scalar = scalar;
	if (!igsd->scalar) {
		hash_si_init(&igsd->strings, 16);
		hash_si_ptr_init(&igsd->references, 16);
		igsd->references_id = 0;
	}

	igsd->compact_strings = (bool)IGBINARY_G(compact_strings);

	igsd->deferred_dtor_tracker.zvals = NULL;
	igsd->deferred_dtor_tracker.count = 0;
	igsd->deferred_dtor_tracker.capacity = 0;

	return r;
}
/* }}} */
/* {{{ igbinary_serialize_data_deinit */
/** Deinits igbinary_serialize_data, freeing the allocated data structures. */
inline static void igbinary_serialize_data_deinit(struct igbinary_serialize_data *igsd, int free_buffer) {
	if (free_buffer && igsd->buffer) {
		efree(igsd->buffer);
	}

	if (!igsd->scalar) {
		hash_si_deinit(&igsd->strings);
		hash_si_ptr_deinit(&igsd->references);
	}
	free_deferred_dtors(&igsd->deferred_dtor_tracker);
}
/* }}} */
/* {{{ igbinary_serialize_header */
/** Serializes header ("\x00\x00\x00\x02"). */
inline static void igbinary_serialize_header(struct igbinary_serialize_data *igsd) {
	uint8_t* append_buffer = igsd->buffer;
	ZEND_ASSERT(igsd->buffer_size == 0);
	ZEND_ASSERT(igsd->buffer_capacity >= 4);
	append_buffer[0] = 0;
	append_buffer[1] = 0;
	append_buffer[2] = 0;
	append_buffer[3] = IGBINARY_FORMAT_VERSION;
	igsd->buffer_size = 4;
}
/* }}} */
static int igbinary_raise_capacity(struct igbinary_serialize_data *igsd, size_t size) {
	do {
		igsd->buffer_capacity *= 2;
	} while (igsd->buffer_size + size >= igsd->buffer_capacity);

	uint8_t *const old_buffer = igsd->buffer;
	igsd->buffer = erealloc(old_buffer, igsd->buffer_capacity);
	if (UNEXPECTED(igsd->buffer == NULL)) {
		/* We failed to allocate a larger buffer for the result. Free the memory used for the original buffer. */
		efree(old_buffer);
		return 1;
	}

	return 0;
}
/* {{{ igbinary_serialize_resize */
/** Expands igbinary_serialize_data if necessary. */
inline static int igbinary_serialize_resize(struct igbinary_serialize_data *igsd, size_t size) {
	if (igsd->buffer_size + size < igsd->buffer_capacity) {
		return 0;
	}

	return igbinary_raise_capacity(igsd, size);
}
/* }}} */
/* {{{ igbinary_serialize8 */
/** Serialize 8bit value. */
inline static int igbinary_serialize8(struct igbinary_serialize_data *igsd, uint8_t i) {
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 1));

	igsd->buffer[igsd->buffer_size++] = i;
	return 0;
}
/* }}} */
/* {{{ igbinary_serialize16 */
/** Serialize 16bit value. */
inline static int igbinary_serialize16(struct igbinary_serialize_data *igsd, uint16_t i) {
	uint8_t *append_buffer;
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 2));

	append_buffer = &igsd->buffer[igsd->buffer_size];
	append_buffer[0] = (uint8_t)(i >> 8 & 0xff);
	append_buffer[1] = (uint8_t)(i & 0xff);
	igsd->buffer_size += 2;

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize32 */
/** Serialize 32bit value. */
inline static int igbinary_serialize32(struct igbinary_serialize_data *igsd, uint32_t i) {
	uint8_t *append_buffer;
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 4));

	append_buffer = &igsd->buffer[igsd->buffer_size];
	append_buffer[0] = (uint8_t)(i >> 24 & 0xff);
	append_buffer[1] = (uint8_t)(i >> 16 & 0xff);
	append_buffer[2] = (uint8_t)(i >> 8 & 0xff);
	append_buffer[3] = (uint8_t)(i & 0xff);
	igsd->buffer_size += 4;

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize64 */
/** Serialize 64bit value. */
inline static int igbinary_serialize64(struct igbinary_serialize_data *igsd, uint64_t i) {
	uint8_t *append_buffer;
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 8));

	append_buffer = &igsd->buffer[igsd->buffer_size];
	append_buffer[0] = (uint8_t)(i >> 56 & 0xff);
	append_buffer[1] = (uint8_t)(i >> 48 & 0xff);
	append_buffer[2] = (uint8_t)(i >> 40 & 0xff);
	append_buffer[3] = (uint8_t)(i >> 32 & 0xff);
	append_buffer[4] = (uint8_t)(i >> 24 & 0xff);
	append_buffer[5] = (uint8_t)(i >> 16 & 0xff);
	append_buffer[6] = (uint8_t)(i >> 8 & 0xff);
	append_buffer[7] = (uint8_t)(i & 0xff);
	igsd->buffer_size += 8;

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize8 */
/** Serialize 8bit value + 8bit value. */
inline static int igbinary_serialize8_and_8(struct igbinary_serialize_data *igsd, uint8_t i, uint8_t v) {
	uint8_t *append_buffer;
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 2));
	append_buffer = &igsd->buffer[igsd->buffer_size];

	append_buffer[0] = i;
	append_buffer[1] = v;
	igsd->buffer_size += 2;
	return 0;
}
/* }}} */
/** Serialize 8bit value + 16bit value. */
inline static int igbinary_serialize8_and_16(struct igbinary_serialize_data *igsd, uint8_t i, uint16_t v) {
	uint8_t *append_buffer;
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 3));
	append_buffer = &igsd->buffer[igsd->buffer_size];

	append_buffer[0] = i;
	append_buffer[1] = (uint8_t)(v >> 8 & 0xff);
	append_buffer[2] = (uint8_t)(v & 0xff);
;
	igsd->buffer_size += 3;
	return 0;
}
/* }}} */
/** Serialize 8bit value + 32bit value. */
inline static int igbinary_serialize8_and_32(struct igbinary_serialize_data *igsd, uint8_t i, uint32_t v) {
	uint8_t *append_buffer;
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 5));
	append_buffer = &igsd->buffer[igsd->buffer_size];

	append_buffer[0] = i;
	append_buffer[1] = (uint8_t)(v >> 24 & 0xff);
	append_buffer[2] = (uint8_t)(v >> 16 & 0xff);
	append_buffer[3] = (uint8_t)(v >> 8 & 0xff);
	append_buffer[4] = (uint8_t)(v & 0xff);
;
	igsd->buffer_size += 5;
	return 0;
}
/* }}} */
/** Serialize 8bit value + 64bit value. */
inline static int igbinary_serialize8_and_64(struct igbinary_serialize_data *igsd, uint8_t i, uint64_t v) {
	uint8_t *append_buffer;
	RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, 9));
	append_buffer = &igsd->buffer[igsd->buffer_size];

	append_buffer[0] = i;
	append_buffer[1] = (uint8_t)(v >> 56 & 0xff);
	append_buffer[2] = (uint8_t)(v >> 48 & 0xff);
	append_buffer[3] = (uint8_t)(v >> 40 & 0xff);
	append_buffer[4] = (uint8_t)(v >> 32 & 0xff);
	append_buffer[5] = (uint8_t)(v >> 24 & 0xff);
	append_buffer[6] = (uint8_t)(v >> 16 & 0xff);
	append_buffer[7] = (uint8_t)(v >> 8 & 0xff);
	append_buffer[8] = (uint8_t)(v & 0xff);
;
	igsd->buffer_size += 9;
	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_null */
/** Serializes null. */
inline static int igbinary_serialize_null(struct igbinary_serialize_data *igsd) {
	return igbinary_serialize8(igsd, igbinary_type_null);
}
/* }}} */
/* {{{ igbinary_serialize_bool */
/** Serializes bool. */
inline static int igbinary_serialize_bool(struct igbinary_serialize_data *igsd, int b) {
	return igbinary_serialize8(igsd, (uint8_t)(b ? igbinary_type_bool_true : igbinary_type_bool_false));
}
/* }}} */
/* {{{ igbinary_serialize_long */
/** Serializes zend_long. */
inline static int igbinary_serialize_long(struct igbinary_serialize_data *igsd, zend_long l) {
	const bool p = l >= 0;
	// k is the absolute value of l
	const zend_ulong k = p ? (zend_ulong)l : -(zend_ulong)l;

	if (k <= 0xff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd,
			p ? igbinary_type_long8p : igbinary_type_long8n,
			(uint8_t)k
		));
	} else if (k <= 0xffff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_16(igsd,
			p ? igbinary_type_long16p : igbinary_type_long16n,
			(uint16_t)k
		));
#if SIZEOF_ZEND_LONG == 8
	} else if (k <= 0xffffffff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_32(igsd,
			p ? igbinary_type_long32p : igbinary_type_long32n,
			(uint32_t)k
		));
	} else {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_64(igsd,
			p ? igbinary_type_long64p : igbinary_type_long64n,
			(uint64_t)k
		));
	}
#elif SIZEOF_ZEND_LONG == 4
	} else {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_32(igsd,
			p ? igbinary_type_long32p : igbinary_type_long32n,
			(uint32_t)k
		));
	}
#else
#error "Strange sizeof(zend_long)."
#endif

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_double */
/** Serializes double. */
inline static int igbinary_serialize_double(struct igbinary_serialize_data *igsd, double d) {
	union {
		double d;
		uint64_t u;
	} u;

	u.d = d;

	return igbinary_serialize8_and_64(igsd, igbinary_type_double, u.u);
}
/* }}} */
/* {{{ igbinary_serialize_string */
/**
 * Serializes string.
 *
 * When compact_strings is true,
 * this will serialize the string as igbinary_type_string* (A length followed by a character array) the first time,
 * and serialize subsequent references to the same string as igbinary_type_string_id*.
 *
 * When compact_strings is false, this will always serialize the string as a character array.
 * compact_strings speeds up serialization, but slows down serialization and uses more space to represent the serialization.
 *
 * Serializes each string once.
 * After first time uses pointers (igbinary_type_string_id*) instead of igbinary_type_string*.
 */
inline static int igbinary_serialize_string(struct igbinary_serialize_data *igsd, zend_string *s) {
	const size_t len = ZSTR_LEN(s);
	if (len == 0) {
		/* The empty string is always serialized as igbinary_serialize_string (1 byte instead of 2) */
		return igbinary_serialize8(igsd, igbinary_type_string_empty);
	}

	if (!igsd->scalar && igsd->compact_strings) {
		struct hash_si_result result = hash_si_find_or_insert(&igsd->strings, s, igsd->string_count);
		if (result.code == hash_si_code_exists) {
			uint32_t value = result.value;
			if (value <= 0xff) {
				RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, (uint8_t)igbinary_type_string_id8, (uint8_t)value));
			} else if (value <= 0xffff) {
				RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_16(igsd, (uint8_t)igbinary_type_string_id16, (uint16_t)value));
			} else {
				RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_32(igsd, (uint8_t)igbinary_type_string_id32, value));
			}
			return 0;
		} else if (EXPECTED(result.code == hash_si_code_inserted)) {
			/* Fall through to igbinary_serialize_chararray */
		} else {
			return 1;  /* Failed to allocate copy of string */
		}
	}

	igsd->string_count++; /* A new string is being serialized - update count so that duplicate class names can be used. */
	return igbinary_serialize_chararray(igsd, ZSTR_VAL(s), len);
}
/* }}} */
/* {{{ igbinary_serialize_chararray */
/** Serializes string data as the type followed by the length followed by the raw character array. */
inline static int igbinary_serialize_chararray(struct igbinary_serialize_data *igsd, const char *s, size_t len) {
	uint8_t *append_buffer;
	int offset;
	if (len <= 0xff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, len + 2));
		append_buffer = &igsd->buffer[igsd->buffer_size];
		append_buffer[0] = igbinary_type_string8;
		append_buffer[1] = len;
		offset = 2;
	} else if (len <= 0xffff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, len + 3));
		append_buffer = &igsd->buffer[igsd->buffer_size];
		append_buffer[0] = igbinary_type_string16;
		append_buffer[1] = (uint8_t)(len >> 8 & 0xff);
		append_buffer[2] = (uint8_t)(len & 0xff);
		offset = 3;
	} else {
		RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, len + 5));
		append_buffer = &igsd->buffer[igsd->buffer_size];
		append_buffer[0] = igbinary_type_string32;
		append_buffer[1] = (uint8_t)(len >> 24 & 0xff);
		append_buffer[2] = (uint8_t)(len >> 16 & 0xff);
		append_buffer[3] = (uint8_t)(len >> 8 & 0xff);
		append_buffer[4] = (uint8_t)(len & 0xff);
		offset = 5;
	}

	memcpy(append_buffer + offset, s, len);
	igsd->buffer_size += offset + len;

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_array */
/**
 * Serializes an array's or object's inner properties.
 * If properties or keys are unexpectedly added (e.g. by __sleep() or serialize() elsewhere), this will skip serializing them.
 * If properties or keys are unexpectedly removed, this will add igbinary_type_null as padding for the corresponding entries.
 */
inline static int igbinary_serialize_array(struct igbinary_serialize_data *igsd, zval *z, bool object, bool incomplete_class, bool serialize_props) {
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
	h = object ? zend_get_properties_for(z, ZEND_PROP_PURPOSE_SERIALIZE) : HASH_OF(z);

	/* hash size */
	n = h ? zend_hash_num_elements(h) : 0;

	/* incomplete class magic member */
	if (n > 0 && incomplete_class) {
		--n;
	}

	ZEND_ASSERT(!object || !serialize_props);
	/* if it is an array or a reference to an array, then add a reference unique to that **reference** to that array */
	if (serialize_props && igbinary_serialize_array_ref(igsd, z_original, false) == 0) {
		return 0;
	}

	if (n <= 0xff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, igbinary_type_array8, n));

		if (n == 0) {
			if (object) {
				zend_release_properties(h);
			}
			return 0;
		}
	} else if (n <= 0xffff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_16(igsd, igbinary_type_array16, n));
	} else {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_32(igsd, igbinary_type_array32, n));
	}

	/* serialize properties. */
	ZEND_HASH_FOREACH_KEY_VAL(h, key_index, key, d) {
		/* skip magic member in incomplete classes */
		if (incomplete_class && strcmp(ZSTR_VAL(key), MAGIC_MEMBER) == 0) {
			continue;
		}
		if (d == NULL) {
			php_error_docref(NULL, E_NOTICE, "Received NULL value from hash.");
			if (object) {
				zend_release_properties(h);
			}
			return 1;
		}

		/* https://wiki.php.net/phpng-int - This is a weak pointer, completely different from a PHP reference (&$foo has a type of IS_REFERENCE) */
		if (Z_TYPE_P(d) == IS_INDIRECT) {
			d = Z_INDIRECT_P(d);
		}
		if (Z_TYPE_P(d) == IS_UNDEF) {
			if (!serialize_props) {
				RETURN_1_IF_NON_ZERO(igbinary_serialize_null(igsd));
				continue;
			}
			ZVAL_NULL(d);
		}

		if (key == NULL) {
			/* Key is numeric */
			RETURN_1_IF_NON_ZERO(igbinary_serialize_long(igsd, key_index));
		} else {
			/* Key is string */
			RETURN_1_IF_NON_ZERO(igbinary_serialize_string(igsd, key));
		}

		/* we should still add element even if it's not OK,
		 * since we already wrote the length of the array before */
		if (Z_TYPE_P(d) == IS_UNDEF) {
			RETURN_1_IF_NON_ZERO(igbinary_serialize_null(igsd));
		} else {
			RETURN_1_IF_NON_ZERO(igbinary_serialize_zval(igsd, d));
		}
	} ZEND_HASH_FOREACH_END();
	if (object) {
		zend_release_properties(h);
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_array_ref */
/** Serializes array reference (or reference in an object). Returns 0 on success. */
inline static int igbinary_serialize_array_ref(struct igbinary_serialize_data *igsd, zval * const z, bool object) {
	size_t t;
	zend_uintptr_t key;  /* The address of the pointer to the zend_refcounted struct or other struct */
	static int INVALID_KEY;  /* Not used, but we take the pointer of this knowing other zvals won't share it*/

	/* Similar to php_var_serialize_intern's first part, as well as php_add_var_hash, for printing R: (reference) or r:(object) */
	/* However, it differs from the built in serialize() in that references to objects are preserved when serializing and unserializing? (TODO check, test for backwards compatibility) */
	zend_bool is_ref = Z_ISREF_P(z);
	/* Do I have to dereference object references so that reference ids will be the same as in php5? */
	/* If I do, then more tests fail. */
	/* is_ref || IS_OBJECT implies it has a unique refcounted struct */
	if (object && Z_TYPE_P(z) == IS_OBJECT) {
		key = (zend_uintptr_t)Z_OBJ_HANDLE_P(z); /* expand object handle(uint32_t), cast to 32-bit/64-bit pointer */
	} else if (is_ref) {
		/* NOTE: PHP switched from `zval*` to `zval` for the values stored in HashTables.
		 * If an array has two references to the same ZVAL, then those references will have different zvals.
		 * We use Z_COUNTED_P(ref), which will be the same if (and only if) the references are the same. */
		/* is_ref implies there is a unique reference counting pointer for the reference */
		key = (zend_uintptr_t)Z_COUNTED_P(z);
	} else if (EXPECTED(Z_TYPE_P(z) == IS_ARRAY)) {
		if (Z_REFCOUNTED_P(z)) {
			key = (zend_uintptr_t)Z_COUNTED_P(z);
		} else {
			/* `z` on its own will almost never be reused. */
			key = (zend_uintptr_t)Z_ARR_P(z);
		}
	} else {
		/* Nothing else is going to reference this when this is serialized, this isn't ref counted or an object, shouldn't be reached. */
		/* Increment the reference id for the deserializer, give up. */
		++igsd->references_id;
		php_error_docref(NULL, E_NOTICE, "igbinary_serialize_array_ref expected either object or reference (param object=%s), got neither (zend_type=%d)", object ? "true" : "false", (int)Z_TYPE_P(z));
		return 1;
	}

	/* FIXME hack? If the top-level element was an array, we assume that it can't be a reference when we serialize it, */
	/* because that's the way it was serialized in php5. */
	/* Does this work with different forms of recursive arrays? */
	if (igsd->references_id == 0 && !object) {
		key = (zend_uintptr_t)&INVALID_KEY;
	}

	t = hash_si_ptr_find_or_insert(&igsd->references, key, igsd->references_id);
	if (t == SIZE_MAX) {
		/* This is a brand new object/array/reference entry with a key equal to igsd->references_id */
		igsd->references_id++;
		/* We only need to call this if the array/object is new, in case __serialize or other methods return temporary arrays or modify arrays that were serialized earlier */
		igsd_addref_and_defer_dtor(&igsd->deferred_dtor_tracker, z);
		return 1;
	}

	/* TODO: Properly handle running out of memory in this helper function. */
	/* It returns 1 both for running out of memory and inserting a new entry. */
	enum igbinary_type type;
	if (t <= 0xff) {
		type = object ? igbinary_type_objref8 : igbinary_type_ref8;
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, (uint8_t)type, t));
	} else if (t <= 0xffff) {
		type = object ? igbinary_type_objref16 : igbinary_type_ref16;
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_16(igsd, (uint8_t)type, t))
	} else {
		type = object ? igbinary_type_objref32 : igbinary_type_ref32;
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_32(igsd, (uint8_t)type, t))
	}
	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_array_sleep_single_prop_value */
/** Serializes one value of an object's properties array, for use with the __sleep function. */
inline static int igbinary_serialize_array_sleep_single_prop_value(struct igbinary_serialize_data *igsd, zval *z, zval *v, zend_class_entry *ce, zend_string *prop_name) {
	/* Precondition: All args are non-null */
	if (Z_TYPE_P(v) == IS_INDIRECT) {
		v = Z_INDIRECT_P(v);
		if (UNEXPECTED(Z_TYPE_P(v) == IS_UNDEF)) {
#if PHP_VERSION_ID >= 70400
			if (UNEXPECTED(zend_get_typed_property_info_for_slot(Z_OBJ_P(z), v) != NULL)) {
				zend_throw_error(NULL, "Typed property %s::$%s must not be accessed before initialization (in __sleep)", ZSTR_VAL(ce->name), ZSTR_VAL(prop_name));
				return 1;
			}
#endif
			goto serialize_untyped_uninitialized_prop;
		}
	} else {
		if (UNEXPECTED(Z_TYPE_P(v) == IS_UNDEF)) {
serialize_untyped_uninitialized_prop:
			php_error_docref(NULL, E_NOTICE, "\"%s\" returned as member variable from __sleep() but does not exist", ZSTR_VAL(prop_name));
			return igbinary_serialize_null(igsd);
		}
	}
	return igbinary_serialize_zval(igsd, v);
}
/* }}} */
/* {{{ igbinary_serialize_array_sleep_inner */
/** Serializes object's properties array with __sleep -function. */
inline static int igbinary_serialize_array_sleep_inner(struct igbinary_serialize_data *igsd, zval *z, HashTable *h, size_t n, HashTable *object_properties, zend_class_entry *ce) {
	zval *d;
	zval *v;

	ZEND_HASH_FOREACH_VAL(h, d) {
		if (UNEXPECTED(d == NULL || Z_TYPE_P(d) != IS_STRING)) {
			php_error_docref(NULL, E_NOTICE, "__sleep should return an array only "
					"containing the names of instance-variables to "
					"serialize");

			/* we should still add element even if it's not OK,
			 * since we already wrote the length of the array before
			 * serialize null as key-value pair */
			/* TODO: Allow creating a tmp string, like php's serialize() */
			RETURN_1_IF_NON_ZERO(igbinary_serialize_null(igsd));
			continue;
		}
		zend_string *prop_name = Z_STR_P(d);

		if ((v = zend_hash_find(object_properties, prop_name)) != NULL) {
			RETURN_1_IF_NON_ZERO(igbinary_serialize_string(igsd, prop_name));

			RETURN_1_IF_NON_ZERO(igbinary_serialize_array_sleep_single_prop_value(igsd, z, v, ce, prop_name));
		} else {
			zend_string *mangled_prop_name;

			v = NULL;

			int res;
			/* try private */
			mangled_prop_name = zend_mangle_property_name(ZSTR_VAL(ce->name), ZSTR_LEN(ce->name),
				ZSTR_VAL(prop_name), ZSTR_LEN(prop_name), 0);
			v = zend_hash_find(object_properties, mangled_prop_name);

			/* try protected */
			if (v == NULL) {
				zend_string_efree(mangled_prop_name);
				mangled_prop_name = zend_mangle_property_name("*", 1,
					ZSTR_VAL(prop_name), ZSTR_LEN(prop_name), 0);

				v = zend_hash_find(object_properties, mangled_prop_name);
				/* Neither protected nor private property exists */
				if (v == NULL) {
					zend_string_efree(mangled_prop_name);

					php_error_docref(NULL, E_NOTICE, "\"%s\" returned as member variable from __sleep() but does not exist", ZSTR_VAL(prop_name));
					RETURN_1_IF_NON_ZERO(igbinary_serialize_string(igsd, prop_name));

					RETURN_1_IF_NON_ZERO(igbinary_serialize_null(igsd));

					return 0;
				}
			}

			res = igbinary_serialize_string(igsd, mangled_prop_name);
			/* igbinary_serialize_string will increase the reference count. */
			zend_string_release_ex(mangled_prop_name, 0);

			RETURN_1_IF_NON_ZERO(res);
			RETURN_1_IF_NON_ZERO(igbinary_serialize_array_sleep_single_prop_value(igsd, z, v, ce, prop_name));
		}
	} ZEND_HASH_FOREACH_END();

	return 0;
}
/* }}} */
/* {{{ igbinary_serialize_array_sleep */
/** Serializes object's properties array with __sleep -function. */
inline static int igbinary_serialize_array_sleep(struct igbinary_serialize_data *igsd, zval *z, HashTable *h, zend_class_entry *ce) {
	HashTable *object_properties;
	size_t n = zend_hash_num_elements(h);

	/* Serialize array id. */
	if (n <= 0xff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, igbinary_type_array8, n))
		if (n == 0) {
			return 0;
		}
	} else if (n <= 0xffff) {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_16(igsd, igbinary_type_array8, n))
	} else {
		RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_32(igsd, igbinary_type_array8, n))
	}

	object_properties = zend_get_properties_for(z, ZEND_PROP_PURPOSE_SERIALIZE);

	int r = igbinary_serialize_array_sleep_inner(igsd, z, h, n, object_properties, ce);
	zend_release_properties(object_properties);
	return r;
}
/* }}} */
/* {{{ igbinary_serialize_object_name */
/**
 * Serialize a PHP object's class name.
 * Note that this deliberately ignores the compact_strings setting.
 */
inline static int igbinary_serialize_object_name(struct igbinary_serialize_data *igsd, zend_string *class_name) {
	struct hash_si_result result = hash_si_find_or_insert(&igsd->strings, class_name, igsd->string_count);
	if (result.code == hash_si_code_inserted) {
		const size_t name_len = ZSTR_LEN(class_name);
		igsd->string_count += 1;

		if (name_len <= 0xff) {
			RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, igbinary_type_object8, name_len))
		} else if (name_len <= 0xffff) {
			RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_16(igsd, igbinary_type_object8, name_len))
		} else {
			RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_32(igsd, igbinary_type_object8, name_len))
		}

		RETURN_1_IF_NON_ZERO(igbinary_serialize_resize(igsd, name_len));

		memcpy(igsd->buffer + igsd->buffer_size, ZSTR_VAL(class_name), name_len);
		igsd->buffer_size += name_len;
	} else if (EXPECTED(result.code == hash_si_code_exists)) {
		/* already serialized string */
		uint32_t value = result.value;
		if (value <= 0xff) {
			RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, (uint8_t)igbinary_type_object_id8, value))
		} else if (value <= 0xffff) {
			RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, (uint8_t)igbinary_type_object_id16, value))
		} else {
			RETURN_1_IF_NON_ZERO(igbinary_serialize8_and_8(igsd, (uint8_t)igbinary_type_object_id32, value))
		}
	} else {
		return 1; /* Failed to allocate copy of string */
	}

	return 0;
}
/* }}} */
/* igbinary_serialize_object_standard_serializer {{{ */
inline static int igbinary_serialize_object_standard_serializer(struct igbinary_serialize_data* igsd, zval *z, zend_class_entry *ce) {
	unsigned char *serialized_data = NULL;
	size_t serialized_len;
	int r = 0;

	if (ce->serialize(z, &serialized_data, &serialized_len, (zend_serialize_data *)NULL) == SUCCESS && !EG(exception)) {
		if (igbinary_serialize_object_name(igsd, ce->name) != 0) {
			if (serialized_data) {
				efree(serialized_data);
			}
			return 1;
		}

		if (serialized_len <= 0xff) {
			if (igbinary_serialize8_and_8(igsd, igbinary_type_object_ser8, serialized_len) != 0) {
				if (serialized_data) {
					efree(serialized_data);
				}
				return 1;
			}
		} else if (serialized_len <= 0xffff) {
			if (igbinary_serialize8_and_16(igsd, (uint8_t)igbinary_type_object_ser16, serialized_len) != 0) {
				if (serialized_data) {
					efree(serialized_data);
				}
				return 1;
			}
		} else {
			if (igbinary_serialize8_and_32(igsd, (uint8_t)igbinary_type_object_ser32, serialized_len) != 0) {
				if (serialized_data) {
					efree(serialized_data);
				}
				return 1;
			}
		}

		if (igbinary_serialize_resize(igsd, serialized_len)) {
			if (serialized_data) {
				efree(serialized_data);
			}

			return 1;
		}

		memcpy(igsd->buffer + igsd->buffer_size, serialized_data, serialized_len);
		igsd->buffer_size += serialized_len;
	} else if (EG(exception)) {
		/* exception, return failure */
		r = 1;
	} else {
		/* Serialization callback failed, assume null output */
		r = igbinary_serialize_null(igsd);
	}

	if (serialized_data) {
		efree(serialized_data);
	}

	return r;
}
/* }}} */
/* igbinary_var_serialize_call_magic_serialize {{{ */
// Source: ext/standard/var.c from php-src
#if PHP_VERSION_ID >= 70400
inline static int igbinary_var_serialize_call_magic_serialize(zval *retval, zval *obj)
{
#if PHP_VERSION_ID >= 80000
	BG(serialize_lock)++;
	zend_call_known_instance_method_with_0_params(
		Z_OBJCE_P(obj)->__serialize, Z_OBJ_P(obj), retval);
	BG(serialize_lock)--;

	if (EG(exception)) {
		zval_ptr_dtor(retval);
		return 1;
	}
#else
	zval fname;
	int res;

	ZVAL_STRINGL(&fname, "__serialize", sizeof("__serialize") - 1);
	// XXX does this work with the standard serializer?
	BG(serialize_lock)++;
	res = call_user_function(CG(function_table), obj, &fname, retval, 0, 0);
	BG(serialize_lock)--;
	zval_ptr_dtor_nogc(&fname);

	if (res == FAILURE || Z_ISUNDEF_P(retval)) {
		zval_ptr_dtor(retval);
		return 1;
	}
#endif

	if (Z_TYPE_P(retval) != IS_ARRAY) {
		zval_ptr_dtor(retval);
		zend_type_error("%s::__serialize() must return an array", ZSTR_VAL(Z_OBJCE_P(obj)->name));
		return 1;
	}

	return 0;
}
#endif
/* }}} */
/* igbinary_serialize_object_new_serializer {{{ */
#if PHP_VERSION_ID >= 70400
inline static int igbinary_serialize_object_new_serializer(struct igbinary_serialize_data* igsd, zval *z, zend_class_entry *ce) {
	zval retval;
	int r = 0;

	// Call retval = __serialize(z), which returns an array or fails.
	if (igbinary_var_serialize_call_magic_serialize(&retval, z)) {
		// retval is already freed
		if (!EG(exception)) {
			// When does this happen?
			igbinary_serialize_null(igsd);
			return 0;
		}
		return 1;
	}
	// fprintf(stderr, "Serialize returned a value of type %d\n", Z_TYPE(retval));
	if (UNEXPECTED(igbinary_serialize_object_name(igsd, ce->name) != 0)) {
		zval_ptr_dtor(&retval);
		return 1;
	}
	// This serializes an object name followed by an array, in the same format used when no serializers exist.
	// object is false because this is the array returned by __serialize() for the object.
	// TODO: Add tests that this works properly when the same array is reused.
	r = igbinary_serialize_array(igsd, &retval, false, false, false);
	zval_ptr_dtor(&retval);
	if (UNEXPECTED(r)) {
		return 1;
	}
	return EG(exception) != NULL;
}
#endif
/* }}} */
/* {{{ igbinary_serialize_object */
/** Serialize object.
 * @see ext/standard/var.c
 * */
inline static int igbinary_serialize_object(struct igbinary_serialize_data *igsd, zval *z) {
	PHP_CLASS_ATTRIBUTES;

	zend_class_entry *ce;

	zval f;
	zval h;

	int r = 0;

	if (igbinary_serialize_array_ref(igsd, z, true) == 0) {
		return 0;
	}

	ce = Z_OBJCE_P(z);

	/* custom serializer */
	if (ce) {
#if PHP_VERSION_ID >= 70400
		if (zend_hash_str_exists(&ce->function_table, "__serialize", sizeof("__serialize")-1)) {
			// fprintf(stderr, "Going to serialize %s\n", ZSTR_VAL(ce->name));
			return igbinary_serialize_object_new_serializer(igsd, z, ce);
		}
#endif
		if (ce->serialize != NULL) {
			return igbinary_serialize_object_standard_serializer(igsd, z, ce);
		}
	}

	/* serialize class name */
	PHP_SET_CLASS_ATTRIBUTES(z);
	if (igbinary_serialize_object_name(igsd, class_name) != 0) {
		PHP_CLEANUP_CLASS_ATTRIBUTES();
		return 1;
	}
	PHP_CLEANUP_CLASS_ATTRIBUTES();

	if (ce && ce != PHP_IC_ENTRY && zend_hash_str_exists(&ce->function_table, "__sleep", sizeof("__sleep") - 1)) {
		/* function name string */
		ZVAL_STRINGL(&f, "__sleep", sizeof("__sleep") - 1);

		ZVAL_UNDEF(&h);
		/* calling z->__sleep */
		r = call_user_function(CG(function_table), z, &f, &h, 0, 0);

		zval_dtor(&f);

		if (r == SUCCESS && !EG(exception)) {
			r = 0;

			if (Z_TYPE(h) == IS_UNDEF) {
				/* FIXME: is this ok? */
				/* Valid, but skip */
			} else if (HASH_OF(&h)) {
				r = igbinary_serialize_array_sleep(igsd, z, HASH_OF(&h), ce);
			} else {
				php_error_docref(NULL, E_NOTICE, "__sleep should return an array only "
						"containing the names of instance-variables to "
						"serialize");

				/* empty array */
				r = igbinary_serialize8_and_8(igsd, igbinary_type_array8, 0);
			}
		} else {
			r = 1;
		}

		/* cleanup */
		zval_ptr_dtor(&h);

		return r;
	} else {
		return igbinary_serialize_array(igsd, z, true, incomplete_class, false);
	}
}
/* }}} */
/* {{{ igbinary_warn_serialize_resource */
static ZEND_COLD int igbinary_warn_serialize_resource(zval *z) {
	const char *resource_type;
	resource_type = zend_rsrc_list_get_rsrc_type(Z_RES_P(z));
	if (!resource_type) {
		resource_type = "Unknown";
	}
	php_error_docref(NULL, E_DEPRECATED, "Cannot serialize resource(%s) and resources may be converted to objects that cannot be serialized in future php releases. Serializing the value as null instead", resource_type);
	return EG(exception) != NULL;
}
/* }}} */
/* {{{ igbinary_serialize_zval */
/** Serialize zval. */
static int igbinary_serialize_zval(struct igbinary_serialize_data *igsd, zval *z) {
	if (Z_ISREF_P(z)) {
		if (Z_REFCOUNT_P(z) >= 2) {
			RETURN_1_IF_NON_ZERO(igbinary_serialize8(igsd, (uint8_t)igbinary_type_ref))

			switch (Z_TYPE_P(Z_REFVAL_P(z))) {
			case IS_ARRAY:
				return igbinary_serialize_array(igsd, z, false, false, true);
			case IS_OBJECT:
				break; /* Fall through */
			default:
				/* Serialize a reference if zval already added */
				if (igbinary_serialize_array_ref(igsd, z, false) == 0) {
					return 0;
				}
				/* Fall through */
			}
		}

		ZVAL_DEREF(z);
	}
	switch (Z_TYPE_P(z)) {
		case IS_RESOURCE:
			if (igbinary_warn_serialize_resource(z)) {
				return 1;
			}
			return igbinary_serialize_null(igsd);
		case IS_OBJECT:
			return igbinary_serialize_object(igsd, z);
		case IS_ARRAY:
			/* if is_ref, then php5 would have called igbinary_serialize_array_ref */
			return igbinary_serialize_array(igsd, z, false, false, true);
		case IS_STRING:
			return igbinary_serialize_string(igsd, Z_STR_P(z));
		case IS_LONG:
			return igbinary_serialize_long(igsd, Z_LVAL_P(z));
		case IS_NULL:
			return igbinary_serialize_null(igsd);
		case IS_UNDEF:
			// https://github.com/igbinary/igbinary/issues/134
			// TODO: In a new major version, could have a separate type for IS_UNDEF, which would unset the property in an object context?
			return igbinary_serialize_null(igsd);
		case IS_TRUE:
			return igbinary_serialize_bool(igsd, 1);
		case IS_FALSE:
			return igbinary_serialize_bool(igsd, 0);
		case IS_DOUBLE:
			return igbinary_serialize_double(igsd, Z_DVAL_P(z));
		default:
			zend_error(E_ERROR, "igbinary_serialize_zval: zval has unknown type %d", (int)Z_TYPE_P(z));
			/* not reached */
			return 1;
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_data_init */
/** Inits igbinary_unserialize_data. */
inline static int igbinary_unserialize_data_init(struct igbinary_unserialize_data *igsd) {
	igsd->buffer = NULL;
	igsd->buffer_end = NULL;
	igsd->buffer_ptr = NULL;

	igsd->strings = NULL;
	igsd->strings_count = 0;
	igsd->strings_capacity = 4;

	igsd->references = NULL;
	igsd->references_count = 0;
	igsd->references_capacity = 4;

	igsd->references = emalloc(sizeof(igsd->references[0]) * igsd->references_capacity);
	if (igsd->references == NULL) {
		return 1;
	}

	igsd->strings = (zend_string **)emalloc(sizeof(zend_string *) * igsd->strings_capacity);
	if (igsd->strings == NULL) {
		/* We failed to allocate memory for strings. Fail and free everything we allocated */
		efree(igsd->references);
		igsd->references = NULL;
		return 1;
	}

	/** Don't bother allocating zvals which __wakeup or __unserialize, probably not common */
	igsd->deferred = NULL;
	igsd->deferred_count = 0;
	igsd->deferred_capacity = 0;
	igsd->deferred_finished = 0;

	igsd->deferred_dtor_tracker.zvals = NULL;
	igsd->deferred_dtor_tracker.count = 0;
	igsd->deferred_dtor_tracker.capacity = 0;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_data_deinit */
/** Deinits igbinary_unserialize_data. */
inline static void igbinary_unserialize_data_deinit(struct igbinary_unserialize_data *igsd) {
	if (igsd->strings) {
		size_t i;
		for (i = 0; i < igsd->strings_count; i++) {
			zend_string *s = igsd->strings[i];
#if ZEND_DEBUG
			ZEND_ASSERT(GC_REFCOUNT(s) >= 1);
#endif
			zend_string_release_ex(s, 0); /* Should only create interned or non-persistent strings when unserializing */
		}

		efree(igsd->strings);
		igsd->strings = NULL;
	}

	if (igsd->references) {
		efree(igsd->references);
		igsd->references = NULL;
	}
	if (igsd->deferred) {
#if PHP_VERSION_ID >= 70400
		size_t i;
		size_t n = igsd->deferred_count;
		for (i = 0; i < n; i++) {
			struct deferred_call *call = &igsd->deferred[i];
			if (call->is_unserialize) {
				if (!igsd->deferred_finished) {
					struct deferred_unserialize_call *unserialize_call = &call->data.unserialize;
					GC_ADD_FLAGS(unserialize_call->object, IS_OBJ_DESTRUCTOR_CALLED);
					zval_ptr_dtor(&unserialize_call->param);
				}
			}
		}
#endif
		efree(igsd->deferred);
	}
	free_deferred_dtors(&igsd->deferred_dtor_tracker);

	return;
}
/* }}} */
/* {{{ igbinary_unserialize_header_emit_warning */
/**
 * Warns about invalid byte headers
 * Precondition: igsd->buffer_size >= 4 */
static ZEND_COLD void igbinary_unserialize_header_emit_warning(struct igbinary_unserialize_data *igsd, int version) {
	int i;
	char buf[9], *it;
	for (i = 0; i < 4; i++) {
		if (!isprint((int)igsd->buffer[i])) {
			if (version != 0 && (((unsigned int)version) & 0xff000000) == (unsigned int)version) {
				// Check if high order byte was set instead of low order byte
				zend_error(E_WARNING, "igbinary_unserialize_header: unsupported version: %u, should be %u or %u (wrong endianness?)", (unsigned int)version, 0x00000001, (unsigned int)IGBINARY_FORMAT_VERSION);
				return;
			}
			// Binary data, or a version number from a future release.
			zend_error(E_WARNING, "igbinary_unserialize_header: unsupported version: %u, should be %u or %u", (unsigned int)version, 0x00000001, (unsigned int)IGBINARY_FORMAT_VERSION);
			return;
		}
	}

	/* To avoid confusion, if the first 4 bytes are all printable, print those instead of the integer representation to make debugging easier. */
	/* E.g. strings such as "a:2:" are emitted when an Array is serialized with serialize() instead of igbinary_serialize(), */
	/* and subsequently passed to igbinary_unserialize instead of unserialize(). */
	for (it = buf, i = 0; i < 4; i++) {
		char c = igsd->buffer[i];
		if (c == '"' || c == '\\') {
			*it++ = '\\';
		}
		*it++ = c;
	}
	*it = '\0';
	zend_error(E_WARNING, "igbinary_unserialize_header: unsupported version: \"%s\"..., should begin with a binary version header of \"\\x00\\x00\\x00\\x01\" or \"\\x00\\x00\\x00\\x%02x\"", buf, (int)IGBINARY_FORMAT_VERSION);
}
/* }}} */
/* {{{ igbinary_unserialize_header */
/** Unserialize header. Check for version. */
inline static int igbinary_unserialize_header(struct igbinary_unserialize_data *igsd) {
	uint32_t version;

	if (IGB_NEEDS_MORE_DATA(igsd, 5)) {
		zend_error(E_WARNING, "igbinary_unserialize_header: expected at least 5 bytes of data, got %u byte(s)", IGB_REMAINING_BYTES(igsd));
		return 1;
	}

	version = igbinary_unserialize32(igsd);

	/* Support older version 1 and the current format 2 */
	if (version == IGBINARY_FORMAT_VERSION || version == 0x00000001) {
		return 0;
	} else {
		igbinary_unserialize_header_emit_warning(igsd, version);
		return 1;
	}
}
/* }}} */
/* {{{ igbinary_unserialize8 */
/** Unserialize 8bit value. */
inline static uint8_t igbinary_unserialize8(struct igbinary_unserialize_data *igsd) {
	return *(igsd->buffer_ptr++);
}
/* }}} */
/* {{{ igbinary_unserialize16 */
/** Unserialize 16bit value. */
inline static uint16_t igbinary_unserialize16(struct igbinary_unserialize_data *igsd) {
	uint16_t ret =
	    ((uint16_t)(igsd->buffer_ptr[0]) << 8) |
	    ((uint16_t)(igsd->buffer_ptr[1]));
	igsd->buffer_ptr += 2;
	return ret;
}
/* }}} */
/* {{{ igbinary_unserialize32 */
/** Unserialize 32bit value. */
inline static uint32_t igbinary_unserialize32(struct igbinary_unserialize_data *igsd) {
	uint32_t ret =
	    ((uint32_t)(igsd->buffer_ptr[0]) << 24) |
	    ((uint32_t)(igsd->buffer_ptr[1]) << 16) |
	    ((uint32_t)(igsd->buffer_ptr[2]) << 8) |
	    ((uint32_t)(igsd->buffer_ptr[3]));
	igsd->buffer_ptr += 4;
	return ret;
}
/* }}} */
/* {{{ igbinary_unserialize64 */
/** Unserialize 64bit value. */
inline static uint64_t igbinary_unserialize64(struct igbinary_unserialize_data *igsd) {
	uint64_t ret =
	    ((uint64_t)((igsd->buffer_ptr[0])) << 56) |
	    ((uint64_t)((igsd->buffer_ptr[1])) << 48) |
	    ((uint64_t)((igsd->buffer_ptr[2])) << 40) |
	    ((uint64_t)((igsd->buffer_ptr[3])) << 32) |
	    ((uint64_t)((igsd->buffer_ptr[4])) << 24) |
	    ((uint64_t)((igsd->buffer_ptr[5])) << 16) |
	    ((uint64_t)((igsd->buffer_ptr[6])) << 8) |
	    ((uint64_t)((igsd->buffer_ptr[7])) << 0);
	igsd->buffer_ptr += 8;
	return ret;
}
/* }}} */
/* {{{ igbinary_unserialize_long */
/** Unserializes zend_long */
inline static int igbinary_unserialize_long(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zend_long *ret) {
	uint32_t tmp32;
#if SIZEOF_ZEND_LONG == 8
	uint64_t tmp64;
#endif

	if (t == igbinary_type_long8p || t == igbinary_type_long8n) {
		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		*ret = (zend_long)(t == igbinary_type_long8n ? -1 : 1) * igbinary_unserialize8(igsd);
	} else if (t == igbinary_type_long16p || t == igbinary_type_long16n) {
		if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		*ret = (zend_long)(t == igbinary_type_long16n ? -1 : 1) * igbinary_unserialize16(igsd);
	} else if (t == igbinary_type_long32p || t == igbinary_type_long32n) {
		if (IGB_NEEDS_MORE_DATA(igsd, 4)) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		/* check for boundaries (perform only one comparison in common case) */
		tmp32 = igbinary_unserialize32(igsd);
#if SIZEOF_ZEND_LONG == 4
		if (UNEXPECTED(tmp32 >= 0x80000000 && (tmp32 > 0x80000000 || t == igbinary_type_long32p))) {
			zend_error(E_WARNING, "igbinary_unserialize_long: 64bit long on 32bit platform?");
			tmp32 = 0; /* t == igbinary_type_long32p ? LONG_MAX : LONG_MIN; */
		}
#endif
		*ret = (zend_long)(t == igbinary_type_long32n ? -1 : 1) * tmp32;
	} else {
		ZEND_ASSERT(t == igbinary_type_long64p || t == igbinary_type_long64n);
#if SIZEOF_ZEND_LONG == 8
		if (IGB_NEEDS_MORE_DATA(igsd, 8)) {
			zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
			return 1;
		}

		/* check for boundaries (perform only one comparison in common case) */
		tmp64 = igbinary_unserialize64(igsd);
		if (UNEXPECTED(tmp64 >= 0x8000000000000000 && (tmp64 > 0x8000000000000000 || t == igbinary_type_long64p))) {
			zend_error(E_WARNING, "igbinary_unserialize_long: too big 64bit long.");
			tmp64 = 0; /* t == igbinary_type_long64p ? LONG_MAX : LONG_MIN */
		}

		*ret = (zend_long)(t == igbinary_type_long64n ? -1 : 1) * tmp64;
#elif SIZEOF_ZEND_LONG == 4
		/* can't put 64bit long into 32bit one, placeholder zero */
		*ret = 0;
		igbinary_unserialize64(igsd);
		zend_error(E_WARNING, "igbinary_unserialize_long: 64bit long on 32bit platform");
#else
#error "Strange sizeof(zend_long)."
#endif
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_double */
/** Unserializes double. */
inline static int igbinary_unserialize_double(struct igbinary_unserialize_data *igsd, enum igbinary_type t, double *ret) {
	union {
		double d;
		uint64_t u;
	} u;

	(void)t;

	if (IGB_NEEDS_MORE_DATA(igsd, 8)) {
		zend_error(E_WARNING, "igbinary_unserialize_double: end-of-data");
		return 1;
	}

	u.u = igbinary_unserialize64(igsd);

	*ret = u.d;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_string */
/** Unserializes string. Unserializes both actual string or by string id. Returns NULL on error. */
inline static zend_string *igbinary_unserialize_string(struct igbinary_unserialize_data *igsd, enum igbinary_type t) {
	size_t i;
	zend_string *zstr;
	if (t == igbinary_type_string_id8 || t == igbinary_type_object_id8) {
		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_string: end-of-data");
			return NULL;
		}
		i = igbinary_unserialize8(igsd);
	} else if (t == igbinary_type_string_id16 || t == igbinary_type_object_id16) {
		if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
			zend_error(E_WARNING, "igbinary_unserialize_string: end-of-data");
			return NULL;
		}
		i = igbinary_unserialize16(igsd);
	} else if (t == igbinary_type_string_id32 || t == igbinary_type_object_id32) {
		if (IGB_NEEDS_MORE_DATA(igsd, 4)) {
			zend_error(E_WARNING, "igbinary_unserialize_string: end-of-data");
			return NULL;
		}
		i = igbinary_unserialize32(igsd);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_string: unknown type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
		return NULL;
	}

	if (i >= igsd->strings_count) {
		zend_error(E_WARNING, "igbinary_unserialize_string: string index is out-of-bounds");
		return NULL;
	}

	zstr = igsd->strings[i];
	// Add one more ref (currently not using any interned strings) - Callers of this will decrease refs as needed
	ZEND_ASSERT(!ZSTR_IS_INTERNED(zstr));
	GC_ADDREF(zstr);
	return zstr;
}
/* }}} */
/* {{{ igbinary_unserialize_chararray */
/** Unserializes chararray of string. Returns NULL on error. */
inline static zend_string *igbinary_unserialize_chararray(struct igbinary_unserialize_data *igsd, enum igbinary_type t) {
	size_t l;
	zend_string *zstr;

	if (t == igbinary_type_string8 || t == igbinary_type_object8) {
		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return NULL;
		}
		l = igbinary_unserialize8(igsd);
		/* Requires converting these into interned strings. Maybe add one-char in v3 of igbinary format?
		if (l == 1) {
			zstr = ZSTR_CHAR((zend_uchar)igsd->buffer[igsd->buffer_offset]);
			igsd->strings[igsd->strings_count] = zstr;
			igsd->strings_count += 1;
			igsd->buffer_offset++;
			return zstr;
		}
		*/
	} else if (t == igbinary_type_string16 || t == igbinary_type_object16) {
		if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return NULL;
		}
		l = igbinary_unserialize16(igsd);
	} else if (t == igbinary_type_string32 || t == igbinary_type_object32) {
		if (IGB_NEEDS_MORE_DATA(igsd, 4)) {
			zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
			return NULL;
		}
		l = igbinary_unserialize32(igsd);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_chararray: unknown type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
		return NULL;
	}
	if (IGB_NEEDS_MORE_DATA(igsd, l)) {
		zend_error(E_WARNING, "igbinary_unserialize_chararray: end-of-data");
		return NULL;
	}

	if (igsd->strings_count + 1 > igsd->strings_capacity) {
		zend_string **new_strings;
		igsd->strings_capacity *= 2;

		new_strings = (zend_string **)erealloc(igsd->strings, sizeof(zend_string *) * igsd->strings_capacity);
		if (new_strings == NULL) {
			// The cleanup function will take care of destroying the allocated zend_strings.
			return NULL;
		}
		igsd->strings = new_strings;
	}

	zstr = zend_string_init((const char*)igsd->buffer_ptr, l, 0);

	igsd->buffer_ptr += l;

	GC_ADDREF(zstr); /* definitely not interned. Add a reference in case the first reference gets deleted before reusing the temporary string */

	igsd->strings[igsd->strings_count] = zstr;
	igsd->strings_count += 1;
	return zstr;
}
/* }}} */
/* {{{ igbinary_unserialize_array */
/** Unserializes a PHP array. */
inline static int igbinary_unserialize_array(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags, zend_bool create_ref) {
	/* WANT_REF means that z will be wrapped by an IS_REFERENCE */
	uint32_t n;
	uint32_t i;

	zval v;

	enum igbinary_type key_type;

	HashTable *h;

	if (t == igbinary_type_array8) {
		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
			return 1;
		}
		n = igbinary_unserialize8(igsd);
		if (n == 0) {
			if (create_ref) {
				/* NOTE: igbinary uses ZVAL_ARR() when reading the created reference, which assumes that the original array is refcounted. */
				/* This would have to be fixed to switch to ZVAL_EMPTY_ARRAY for the empty array (there's probably not benefit to making that switch). */
				/* Otherwise, using ZVAL_EMPTY_ARRAY would segfault. */
#if PHP_VERSION_ID >= 70300
				ZVAL_EMPTY_ARRAY(z);
#else
				array_init_size(z, 0);
#endif
				struct igbinary_value_ref ref;
				if (flags & WANT_REF) {
					ZVAL_NEW_REF(z, z);
					ref.reference.reference = Z_REF_P(z);
					ref.type = IG_REF_IS_REFERENCE;
				} else {
#if PHP_VERSION_ID >= 70300
					ref.type = IG_REF_IS_EMPTY_ARRAY;
#else
					ref.reference.array = Z_ARR_P(z);
					ref.type = IG_REF_IS_ARRAY;
#endif
				}
				/* add the new array to the list of unserialized references */
				RETURN_1_IF_NON_ZERO(igsd_append_ref(igsd, ref) == SIZE_MAX);
			} else {
				/* This only matters if __unserialize() would get called with an empty array */
#if PHP_VERSION_ID >= 70300
				ZVAL_EMPTY_ARRAY(z);
#else
				array_init_size(z, 0);
#endif
			}
			return 0;
		}
	} else if (t == igbinary_type_array16) {
		if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
			return 1;
		}
		n = igbinary_unserialize16(igsd);
	} else if (t == igbinary_type_array32) {
		if (IGB_NEEDS_MORE_DATA(igsd, 4)) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
			return 1;
		}
		n = igbinary_unserialize32(igsd);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_array: unknown type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
		return 1;
	}

	/* n cannot be larger than the number of minimum "objects" in the array */
	if (IGB_NEEDS_MORE_DATA(igsd, n)) {
		zend_error(E_WARNING, "igbinary_unserialize_array: data size %zu smaller that requested array length %zu.", (size_t)IGB_REMAINING_BYTES(igsd), (size_t)n);
		return 1;
	}

	zval *z_deref = z;
	if (flags & WANT_REF) {
		if (!Z_ISREF_P(z)) {
			ZVAL_NEW_REF(z, z);
			z_deref = Z_REFVAL_P(z);
		}
	}
	array_init_size(z_deref, n);
	if (create_ref) {
		/* Only create a reference if this is not from __unserialize(), because the existence of __unserialize can change */
		struct igbinary_value_ref ref;
		if (flags & WANT_REF) {
			// We converted to reference earlier.
			ref.reference.reference = Z_REF_P(z);
			ref.type = IG_REF_IS_REFERENCE;
		} else {
			ref.reference.array = Z_ARR_P(z_deref);
			ref.type = IG_REF_IS_ARRAY;
		}
		/* add the new array to the list of unserialized references */
		RETURN_1_IF_NON_ZERO(igsd_append_ref(igsd, ref) == SIZE_MAX);
	}

	h = HASH_OF(z_deref);
	for (i = 0; i < n; i++) {
		zval *vp;
		zend_long key_index = 0;
		zend_string *key_str = NULL; /* NULL means use key_index */

		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_array: end-of-data");
cleanup:
			zval_dtor(z);
			ZVAL_NULL(z);
			return 1;
		}

		key_type = (enum igbinary_type)igbinary_unserialize8(igsd);

		switch (key_type) {
			case igbinary_type_long8p:
				/* Manually inline igbinary_unserialize_long() for array keys from 0 to 255, because they're the most common among integers. */
				if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
					zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
					goto cleanup;
				}

				key_index = igbinary_unserialize8(igsd);
				break;
			case igbinary_type_long16p:
				/* and for array keys from 0 to 65535. */
				if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
					zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
					goto cleanup;
				}

				key_index = igbinary_unserialize16(igsd);
				break;
			case igbinary_type_long8n:
			case igbinary_type_long16n:
			case igbinary_type_long32p:
			case igbinary_type_long32n:
			case igbinary_type_long64p:
			case igbinary_type_long64n:
				if (UNEXPECTED(igbinary_unserialize_long(igsd, key_type, &key_index))) {
					goto cleanup;
				}
				break;
			case igbinary_type_string_id8:
			case igbinary_type_string_id16:
			case igbinary_type_string_id32:
				key_str = igbinary_unserialize_string(igsd, key_type);
				if (UNEXPECTED(key_str == NULL)) {
					goto cleanup;
				}
				break;
			case igbinary_type_string8:
			case igbinary_type_string16:
			case igbinary_type_string32:
				key_str = igbinary_unserialize_chararray(igsd, key_type);
				if (UNEXPECTED(key_str == NULL)) {
					goto cleanup;
				}
				break;
			case igbinary_type_string_empty:
				key_str = ZSTR_EMPTY_ALLOC();
				break;
			case igbinary_type_null:
				continue;
			default:
				zend_error(E_WARNING, "igbinary_unserialize_array: unknown key type '%02x', position %zu", key_type, (size_t)IGB_BUFFER_OFFSET(igsd));
				goto cleanup;
		}

		/* first add key into array so references can properly and not stack allocated zvals */
		/* Use NULL because inserting UNDEF into array does not add a new element */
		ZVAL_NULL(&v);
		if (key_str != NULL) {
			if (UNEXPECTED((vp = zend_hash_find(h, key_str)) != NULL)) {
				if (UNEXPECTED(igsd_addref_and_defer_dtor(&igsd->deferred_dtor_tracker, vp))) {
					return 1;
				}
			} else {
				vp = zend_hash_add_new(h, key_str, &v);
			}

			zend_string_release(key_str);
		} else {
			if (UNEXPECTED((vp = zend_hash_index_find(h, key_index)) != NULL)) {
				if (UNEXPECTED(igsd_addref_and_defer_dtor(&igsd->deferred_dtor_tracker, vp))) {
					return 1;
				}
			} else {
				vp = zend_hash_index_add_new(h, key_index, &v);
			}
		}

		ZEND_ASSERT(vp != NULL);
		if (Z_TYPE_P(vp) == IS_INDIRECT) {
			/* TODO: In php 8.1+, IS_INDIRECT checks and macros may become unnecessary
			 * due to $GLOBALS being converted to a regular array in https://wiki.php.net/rfc/restrict_globals_usage */
			vp = Z_INDIRECT_P(vp);
		}

		ZEND_ASSERT(vp != NULL);
		if (igbinary_unserialize_zval(igsd, vp, WANT_CLEAR)) {
			return 1;
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_object_properties */
/** Unserializes the array of object properties and adds those to the object z. */
inline static int igbinary_unserialize_object_properties(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z) {
	/* WANT_REF means that z will be wrapped by an IS_REFERENCE */
	uint32_t n;
	uint32_t i;

	zval v;
	zval *vp;
	zval *z_deref;

	enum igbinary_type key_type;

	HashTable *h;
	zend_bool did_extend;

	if (t == igbinary_type_array8) {
		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_object_properties: end-of-data");
			return 1;
		}
		n = igbinary_unserialize8(igsd);
	} else if (t == igbinary_type_array16) {
		if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
			zend_error(E_WARNING, "igbinary_unserialize_object_properties: end-of-data");
			return 1;
		}
		n = igbinary_unserialize16(igsd);
	} else if (t == igbinary_type_array32) {
		if (IGB_NEEDS_MORE_DATA(igsd, 4)) {
			zend_error(E_WARNING, "igbinary_unserialize_object_properties: end-of-data");
			return 1;
		}
		n = igbinary_unserialize32(igsd);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_object_properties: unknown type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
		return 1;
	}

	/* n cannot be larger than the number of minimum "objects" in the array */
	if (IGB_NEEDS_MORE_DATA(igsd, n)) {
		zend_error(E_WARNING, "%s: data size %zu smaller that requested array length %zu.", "igbinary_unserialize_object_properties", (size_t)IGB_REMAINING_BYTES(igsd), (size_t)n);
		return 1;
	}

	z_deref = z;
	ZVAL_DEREF(z_deref);

	/* empty array */
	if (n == 0) {
		return 0;
	}

	h = HASH_OF(z_deref);

	did_extend = 0;

	for (i = 0; i < n; i++) {
		zend_string *key_str = NULL; /* NULL means use key_index */

		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_object_properties: end-of-data");
			zval_dtor(z);
			ZVAL_NULL(z);
			return 1;
		}

		key_type = (enum igbinary_type)igbinary_unserialize8(igsd);

		switch (key_type) {
			case igbinary_type_long8p:
			case igbinary_type_long8n:
			case igbinary_type_long16p:
			case igbinary_type_long16n:
			case igbinary_type_long32p:
			case igbinary_type_long32n:
			case igbinary_type_long64p:
			case igbinary_type_long64n:
			{
				zend_long key_index = 0;
				if (igbinary_unserialize_long(igsd, key_type, &key_index)) {
					zval_dtor(z);
					ZVAL_UNDEF(z);
					return 1;
				}
				key_str = zend_long_to_str(key_index);
				if (UNEXPECTED(key_str == NULL)) {
					zval_dtor(z);
					ZVAL_UNDEF(z);
					return 1;
				}
				break;
			}
			case igbinary_type_string_id8:
			case igbinary_type_string_id16:
			case igbinary_type_string_id32:
				key_str = igbinary_unserialize_string(igsd, key_type);
				if (UNEXPECTED(key_str == NULL)) {
					zval_dtor(z);
					ZVAL_UNDEF(z);
					return 1;
				}
				break;
			case igbinary_type_string8:
			case igbinary_type_string16:
			case igbinary_type_string32:
				key_str = igbinary_unserialize_chararray(igsd, key_type);
				if (UNEXPECTED(key_str == NULL)) {
					zval_dtor(z);
					ZVAL_UNDEF(z);
					return 1;
				}
				break;
			case igbinary_type_string_empty:
				key_str = ZSTR_EMPTY_ALLOC();
				break;
			case igbinary_type_null:
				continue;  /* Skip unserializing this element, serialized with no value. In C, this applies to loop, not switch. */
			default:
				zend_error(E_WARNING, "igbinary_unserialize_object_properties: unknown key type '%02x', position %zu", key_type, (size_t)IGB_BUFFER_OFFSET(igsd));
				zval_dtor(z);
				ZVAL_UNDEF(z);
				return 1;
		}

		/* first add key into array so references can properly and not stack allocated zvals */
		/* Use NULL because inserting UNDEF into array does not add a new element */
		ZVAL_NULL(&v);
		zval *prototype_value = zend_hash_find(h, key_str);
#if PHP_VERSION_ID >= 70400
		zend_property_info *info = NULL;
#endif
		if (prototype_value != NULL) {
			if (Z_TYPE_P(prototype_value) == IS_INDIRECT) {
				/* This is a declared object property */
				prototype_value = Z_INDIRECT_P(prototype_value);
#if PHP_VERSION_ID >= 70400
				info = zend_get_typed_property_info_for_slot(Z_OBJ_P(z_deref), prototype_value);
#endif
			}
			/* This is written to avoid the overhead of a second zend_hash_update call. See https://github.com/php/php-src/pull/5095 */
			/* Use igsd_addref_and_defer_dtor instead (like php-src), in case of gc causing issues. */
			/* Something already added a reference, so just defer the destructor */
			if (UNEXPECTED(igsd_defer_dtor(&igsd->deferred_dtor_tracker, prototype_value))) {
				zend_string_release(key_str);
				return 1;
			}
			/* Just override the original value directly */
			ZVAL_COPY_VALUE(prototype_value, &v);
			vp = prototype_value;
		} else {
			if (!did_extend) {
				/* n is at least one, because we're looping from 0..n-1 */
				uint32_t remaining_elements = n - i;
				/* Copied from var_unserializer.re. Need to ensure that IGB_REF_VAL doesn't point to invalid data. */
				/* Worst case: All remaining_elements of the added properties are dynamic. */
				zend_hash_extend(h, zend_hash_num_elements(h) + remaining_elements, (h->u.flags & HASH_FLAG_PACKED));
				did_extend = 1;
			}
			vp = zend_hash_add_new(h, key_str, &v);
		}

		zend_string_release(key_str);

		ZEND_ASSERT(vp != NULL);
		if (Z_TYPE_P(vp) == IS_INDIRECT) {
			vp = Z_INDIRECT_P(vp);
		}

		ZEND_ASSERT(vp != NULL);
		if (igbinary_unserialize_zval(igsd, vp, WANT_CLEAR)) {
			/* Unserializing a property into this zval has failed. */
			/* zval_ptr_dtor(z); */
			/* zval_ptr_dtor(vp); */
			return 1;
		}
#if PHP_VERSION_ID >= 70400
		if (UNEXPECTED(info)) {
			if (!zend_verify_prop_assignable_by_ref(info, vp, /* strict */ 1)) {
				zval_ptr_dtor(vp);
				ZVAL_UNDEF(vp);
				return 1;
			}
			if (Z_ISREF_P(vp)) {
				ZEND_REF_ADD_TYPE_SOURCE(Z_REF_P(vp), info);
			}
		}
#endif
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_object_ser */
/** Unserializes object's property array. This is used to serialize objects implementing Serializable -interface. */
inline static int igbinary_unserialize_object_ser(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, zend_class_entry *ce) {
	size_t n;
	int ret;
	php_unserialize_data_t var_hash;

	if (ce->unserialize == NULL) {
		/* Should be impossible. */
		zend_error(E_WARNING, "Class %s has no unserializer", ZSTR_VAL(ce->name));
		return 1;
	}

	if (t == igbinary_type_object_ser8) {
		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
			return 1;
		}
		n = igbinary_unserialize8(igsd);
	} else if (t == igbinary_type_object_ser16) {
		if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
			zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
			return 1;
		}
		n = igbinary_unserialize16(igsd);
	} else if (t == igbinary_type_object_ser32) {
		if (IGB_NEEDS_MORE_DATA(igsd, 4)) {
			zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
			return 1;
		}
		n = igbinary_unserialize32(igsd);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_object_ser: unknown type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
		return 1;
	}

	if (IGB_NEEDS_MORE_DATA(igsd, n)) {
		zend_error(E_WARNING, "igbinary_unserialize_object_ser: end-of-data");
		return 1;
	}

	PHP_VAR_UNSERIALIZE_INIT(var_hash);
	ret = ce->unserialize(
		z,
		ce,
		(const unsigned char *)igsd->buffer_ptr,
		n,
		(zend_unserialize_data *)&var_hash
	);
	PHP_VAR_UNSERIALIZE_DESTROY(var_hash);

	if (ret != SUCCESS || EG(exception)) {
		return 1;
	}

	igsd->buffer_ptr += n;

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_object */
/** Unserialize an object.
 * @see ext/standard/var_unserializer.c in the php-src repo. Parts of this code are based on that.
 */
inline static int igbinary_unserialize_object(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *z, int flags) {
	zend_class_entry *ce;

	size_t ref_n;

	zend_string *class_name;

	int r;

	bool incomplete_class = false;
	bool is_from_serialized_data = false;

	if (t == igbinary_type_object8 || t == igbinary_type_object16 || t == igbinary_type_object32) {
		class_name = igbinary_unserialize_chararray(igsd, t);
	} else if (t == igbinary_type_object_id8 || t == igbinary_type_object_id16 || t == igbinary_type_object_id32) {
		class_name = igbinary_unserialize_string(igsd, t);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_object: unknown object type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
		return 1;
	}

	if (class_name == NULL) {
		return 1;
	}

	do {
		zval user_func;
		zval retval;
		zval args[1];
		const char* user_func_name;

		/* Try to find class directly */
		if (EXPECTED((ce = zend_lookup_class(class_name)) != NULL)) {
			/* FIXME: lookup class may cause exception in load callback */
			break;
		}

		user_func_name = PG(unserialize_callback_func);
		/* Check for unserialize callback */
		if ((user_func_name == NULL) || (user_func_name[0] == '\0')) {
			incomplete_class = 1;
			ce = PHP_IC_ENTRY;
			break;
		}

		/* Call unserialize callback */
		ZVAL_STRING(&user_func, user_func_name);
		/* FIXME: Do we need a str copy? */
		/* FIXME: Release arg[0] */
		/* FIXME: Release class_name */
		ZVAL_STR_COPY(&args[0], class_name);
		if (call_user_function(CG(function_table), NULL, &user_func, &retval, 1, args) != SUCCESS) {
			php_error_docref(NULL, E_WARNING, "defined (%s) but not found", Z_STRVAL(user_func));
			incomplete_class = 1;
			ce = PHP_IC_ENTRY;
			zval_ptr_dtor_nogc(&args[0]);
			zval_ptr_dtor_nogc(&user_func);
			break;
		}
		/* FIXME: always safe? */
		zval_ptr_dtor(&retval);

		/* The callback function may have defined the class */
		ce = zend_lookup_class(class_name);
		if (!ce) {
			php_error_docref(NULL, E_WARNING, "Function %s() hasn't defined the class it was called for", PG(unserialize_callback_func));
			incomplete_class = true;
			ce = PHP_IC_ENTRY;
		}

		zval_ptr_dtor_nogc(&args[0]);
		zval_ptr_dtor_nogc(&user_func);
	} while (0);

	/* previous user function call may have raised an exception */
	if (EG(exception)) {
		zend_string_release(class_name);
		return 1;
	}

	/* add this to the list of unserialized references, get the index */
	if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
		zend_error(E_WARNING, "igbinary_unserialize_object: end-of-data");
		zend_string_release(class_name);
		return 1;
	}

	{
		/* The actual value of ref is unused. We use ref_n later in this function, after creating the object. */
		struct igbinary_value_ref ref = {{0}, 0};
		ref_n = igsd_append_ref(igsd, ref);
		if (UNEXPECTED(ref_n == SIZE_MAX)) {
			zend_string_release(class_name);
			return 1;
		}
	}

	t = (enum igbinary_type)igbinary_unserialize8(igsd);
	switch (t) {
		case igbinary_type_array8:
		case igbinary_type_array16:
		case igbinary_type_array32:
		{
			if (UNEXPECTED(object_init_ex(z, ce) != SUCCESS)) {
				php_error_docref(NULL, E_NOTICE, "igbinary unable to create object for class entry");
				r = 1;
				break;
			}
			if (incomplete_class) {
#if PHP_VERSION_ID >= 80000
				php_store_class_name(z, class_name);
#else
				php_store_class_name(z, ZSTR_VAL(class_name), ZSTR_LEN(class_name));
#endif
#if PHP_VERSION_ID >= 70400
			} else {
#if PHP_VERSION_ID >= 80000
				if (ce->__unserialize)
#else
				if (zend_hash_str_exists(&ce->function_table, "__unserialize", sizeof("__unserialize") - 1))
#endif
				{
					ZEND_ASSERT(Z_TYPE_P(z) == IS_OBJECT);
					struct igbinary_value_ref *ref = &IGB_REF_VAL_2(igsd, ref_n);
					if ((flags & WANT_REF) != 0) {
						ZVAL_MAKE_REF(z);
						ref->reference.reference = Z_REF_P(z);
						ref->type = IG_REF_IS_REFERENCE;
					} else {
						ref->reference.object = Z_OBJ_P(z);
						ref->type = IG_REF_IS_OBJECT;
					}
					/* Unserialize the array as an array for a deferred call to __unserialize */
					zval param;
					int result;
					zend_string_release(class_name);
					result = igbinary_unserialize_array(igsd, t, &param, 0, false);
					ZVAL_DEREF(z);
					ZEND_ASSERT(Z_TYPE_P(z) == IS_OBJECT);
					igsd_defer_unserialize(igsd, Z_OBJ_P(z), param);
					return result;
				}
#endif
			}
			struct igbinary_value_ref *ref = &IGB_REF_VAL_2(igsd, ref_n);
			if ((flags & WANT_REF) != 0) {
				ZVAL_MAKE_REF(z);
				ref->reference.reference = Z_REF_P(z);
				ref->type = IG_REF_IS_REFERENCE;
			} else {
				ref->reference.object = Z_OBJ_P(z);
				ref->type = IG_REF_IS_OBJECT;
			}

			r = igbinary_unserialize_object_properties(igsd, t, z);
			break;
		}
		case igbinary_type_object_ser8:
		case igbinary_type_object_ser16:
		case igbinary_type_object_ser32:
		{
			is_from_serialized_data = true;
			/* FIXME will this break if z isn't an object? */
			r = igbinary_unserialize_object_ser(igsd, t, z, ce);
			if (r != 0) {
				break;
			}

			if (incomplete_class) {
#if PHP_VERSION_ID >= 80000
				php_store_class_name(z, class_name);
#else
				php_store_class_name(z, ZSTR_VAL(class_name), ZSTR_LEN(class_name));
#endif
			}
			struct igbinary_value_ref *ref = &IGB_REF_VAL_2(igsd, ref_n);
			if ((flags & WANT_REF) != 0) {
				ZVAL_MAKE_REF(z);
				ref->reference.reference = Z_REF_P(z);
				ref->type = IG_REF_IS_REFERENCE;
			} else {
				ref->reference.object = Z_OBJ_P(z);
				ref->type = IG_REF_IS_OBJECT;
			}
			break;
		}
		default:
			zend_error(E_WARNING, "igbinary_unserialize_object: unknown object inner type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
			r = 1;
	}
	zend_string_release(class_name);
	class_name = NULL;

	/* If unserialize was successful, call __wakeup if __wakeup exists for this object. */
	/* (But don't call __wakeup() if Serializable::unserialize was called */
	if (r == 0 && !is_from_serialized_data) {
		struct igbinary_value_ref *const ref = &IGB_REF_VAL_2(igsd, ref_n);
		zend_object *object;
		if (ref->type == IG_REF_IS_OBJECT) {
			object = ref->reference.object;
		} else if (ref->type == IG_REF_IS_REFERENCE) {
			zval ztemp = ref->reference.reference->val;
			if (Z_TYPE(ztemp) != IS_OBJECT) {
				zend_error(E_WARNING, "igbinary_unserialize_object preparing to __wakeup/__unserialize: got reference to non-object somehow (inner type '%02x', position %zu)", t, (size_t)IGB_BUFFER_OFFSET(igsd));
				return 1;
			}
			object = Z_OBJ(ztemp);
		} else {
			zend_error(E_WARNING, "igbinary_unserialize_object preparing to __wakeup/__unserialize: created non-object somehow (inner type '%02x', position %zu)", t, (size_t)IGB_BUFFER_OFFSET(igsd));
			return 1;
		}
		/* May have created a reference while deserializing an object, if it was recursive. */
		if (object->ce != PHP_IC_ENTRY) {
			if (zend_hash_str_exists(&object->ce->function_table, "__wakeup", sizeof("__wakeup") - 1)) {
				if (igsd_defer_wakeup(igsd, object)) {
					return 1;
				}
			}
		}
	}

	/* ZVAL_COPY_VALUE(z, IGB_REF_VAL(igsd, ref_n)); */

	return r;
}
/* }}} */
/* {{{ igbinary_unserialize_ref */
/** Unserializes an array or object by reference. */
inline static int igbinary_unserialize_ref(struct igbinary_unserialize_data *igsd, enum igbinary_type t, zval *const z, int flags) {
	size_t n;

	if (t == igbinary_type_ref8 || t == igbinary_type_objref8) {
		if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
			zend_error(E_WARNING, "igbinary_unserialize_ref: end-of-data");
			return 1;
		}
		n = igbinary_unserialize8(igsd);
	} else if (t == igbinary_type_ref16 || t == igbinary_type_objref16) {
		if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
			zend_error(E_WARNING, "igbinary_unserialize_ref: end-of-data");
			return 1;
		}
		n = igbinary_unserialize16(igsd);
	} else if (t == igbinary_type_ref32 || t == igbinary_type_objref32) {
		if (IGB_NEEDS_MORE_DATA(igsd, 4)) {
			zend_error(E_WARNING, "igbinary_unserialize_ref: end-of-data");
			return 1;
		}
		n = igbinary_unserialize32(igsd);
	} else {
		zend_error(E_WARNING, "igbinary_unserialize_ref: unknown type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
		return 1;
	}

	if (n >= igsd->references_count) {
		zend_error(E_WARNING, "igbinary_unserialize_ref: invalid reference %zu >= %zu", (size_t)n, (size_t)igsd->references_count);
		return 1;
	}

	if (z != NULL) {
		/* FIXME: check with is refcountable or some such */
		zval_ptr_dtor(z);
		ZVAL_UNDEF(z);
	}

	struct igbinary_value_ref *ref_ptr = &IGB_REF_VAL_2(igsd, n);
	struct igbinary_value_ref ref = *ref_ptr;

	/**
	 * Permanently convert the zval in IGB_REF_VAL() into a IS_REFERENCE if it wasn't already one.
	 * TODO: Can there properly be multiple reference groups to an object?
	 * Similar to https://github.com/php/php-src/blob/master/ext/standard/var_unserializer.re , for "R:"
	 * Using `flags` because igbinary_unserialize_ref might be used both for copy on writes ($a = $b = [2]) and by PHP references($a = &$b).
	 */
	if ((flags & WANT_REF) != 0) {
		/* Want to create an IS_REFERENCE, not just to share the same value until modified. */
		switch (ref.type) {
		case IG_REF_IS_OBJECT:
			ZVAL_OBJ(z, ref.reference.object);
			Z_ADDREF_P(z);
			ZVAL_MAKE_REF(z); /* Convert original zval data to a reference */
			/* replace the entry in IGB_REF_VAL with a reference. */
			ref_ptr->reference.reference = Z_REF_P(z);
			ref_ptr->type = IG_REF_IS_REFERENCE;
			break;
		case IG_REF_IS_ARRAY:
			ZVAL_ARR(z, ref.reference.array);
			/* All arrays built by igbinary when unserializing are refcounted, except IG_REF_IS_EMPTY_ARRAY. */
			/* If they were not refcounted, the ZVAL_ARR call would probably also need to be changed. */
			Z_ADDREF_P(z);
			ZVAL_MAKE_REF(z); /* Convert original zval data to a reference */
			/* replace the entry in IGB_REF_VAL with a reference. */
			ref_ptr->reference.reference = Z_REF_P(z);
			ref_ptr->type = IG_REF_IS_REFERENCE;
			break;
#if PHP_VERSION_ID >= 70300
		case IG_REF_IS_EMPTY_ARRAY:
			ZVAL_EMPTY_ARRAY(z);
			ZVAL_MAKE_REF(z); /* Convert original zval data to a reference */
			/* replace the entry in IGB_REF_VAL with a reference. */
			ref_ptr->reference.reference = Z_REF_P(z);
			ref_ptr->type = IG_REF_IS_REFERENCE;
			break;
#endif
		case IG_REF_IS_REFERENCE:
			// This is already a reference, convert into reference count.
			ZVAL_REF(z, ref.reference.reference);
			Z_ADDREF_P(z);
			break;
		}
	} else {
		switch (ref.type) {
		case IG_REF_IS_OBJECT:
			ZVAL_OBJ(z, ref.reference.object);
			Z_ADDREF_P(z);
			break;
		case IG_REF_IS_ARRAY:
			ZVAL_ARR(z, ref.reference.array);
			Z_ADDREF_P(z);
			break;
#if PHP_VERSION_ID >= 70300
		case IG_REF_IS_EMPTY_ARRAY:
			ZVAL_EMPTY_ARRAY(z);
			break;
#endif
		case IG_REF_IS_REFERENCE:
			ZVAL_COPY(z, &(ref.reference.reference->val));
			break;
		}
	}

	return 0;
}
/* }}} */
/* {{{ igbinary_unserialize_zval */
/** Unserialize a zval of any serializable type (zval is PHP's internal representation of a value). */
static int igbinary_unserialize_zval(struct igbinary_unserialize_data *igsd, zval *const z, int flags) {
	enum igbinary_type t;

	zend_long tmp_long;
	double tmp_double;
	zend_string *tmp_str;

	if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
		zend_error(E_WARNING, "igbinary_unserialize_zval: end-of-data");
		return 1;
	}

	t = (enum igbinary_type)igbinary_unserialize8(igsd);

	switch (t) {
		case igbinary_type_ref:
			if (igbinary_unserialize_zval(igsd, z, WANT_REF)) {
				return 1;
			}

			/* If it is already a ref, nothing to do */
			if (Z_ISREF_P(z)) {
				break;
			}

			const zend_uchar type = Z_TYPE_P(z);
			/* Permanently convert the zval in IGB_REF_VAL() into a IS_REFERENCE if it wasn't already one. */
			/* TODO: Support multiple reference groups to the same object */
			/* Similar to https://github.com/php/php-src/blob/master/ext/standard/var_unserializer.re , for "R:" */
			ZVAL_MAKE_REF(z);
			switch (type) {
				case IS_STRING:
				case IS_LONG:
				case IS_NULL:
				case IS_DOUBLE:
				case IS_FALSE:
				case IS_TRUE:
				{
					struct igbinary_value_ref ref;
					ref.reference.reference = Z_REF_P(z);
					ref.type = IG_REF_IS_REFERENCE;
					/* add the unserialized scalar to the list of unserialized references. Objects and arrays were already added in igbinary_unserialize_zval. */
					RETURN_1_IF_NON_ZERO(igsd_append_ref(igsd, ref) == SIZE_MAX);
					break;
				}
				default:
					break;
			}
			break;
		case igbinary_type_objref8:
		case igbinary_type_objref16:
		case igbinary_type_objref32:
		case igbinary_type_ref8:
		case igbinary_type_ref16:
		case igbinary_type_ref32:
			if (igbinary_unserialize_ref(igsd, t, z, flags)) {
				return 1;
			}
			break;
		case igbinary_type_object8:
		case igbinary_type_object16:
		case igbinary_type_object32:
		case igbinary_type_object_id8:
		case igbinary_type_object_id16:
		case igbinary_type_object_id32:
			if (igbinary_unserialize_object(igsd, t, z, flags)) {
				return 1;
			}
			break;
		case igbinary_type_array8:
		case igbinary_type_array16:
		case igbinary_type_array32:
			if (igbinary_unserialize_array(igsd, t, z, flags, true)) {
				return 1;
			}
			break;
		case igbinary_type_string_empty:
			ZVAL_EMPTY_STRING(z);
			break;
		case igbinary_type_string_id8:
		case igbinary_type_string_id16:
		case igbinary_type_string_id32:
			tmp_str = igbinary_unserialize_string(igsd, t);
			if (tmp_str == NULL) {
				return 1;
			}
			ZVAL_STR(z, tmp_str);
			break;
		case igbinary_type_string8:
		case igbinary_type_string16:
		case igbinary_type_string32:
			tmp_str = igbinary_unserialize_chararray(igsd, t);
			if (tmp_str == NULL) {
				return 1;
			}
			ZVAL_STR(z, tmp_str);
			break;
		case igbinary_type_long8p:
			/* Manually inline igbinary_unserialize_long() for values from 0 to 255, because they're the most common among integers in many applications. */
			if (IGB_NEEDS_MORE_DATA(igsd, 1)) {
				zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
				return 1;
			}

			ZVAL_LONG(z, igbinary_unserialize8(igsd));
			break;
		case igbinary_type_long16p:
			/* Manually inline igbinary_unserialize_long() for values from 0 to 255, because they're the most common among integers in many applications. */
			if (IGB_NEEDS_MORE_DATA(igsd, 2)) {
				zend_error(E_WARNING, "igbinary_unserialize_long: end-of-data");
				return 1;
			}

			ZVAL_LONG(z, igbinary_unserialize16(igsd));
			break;
		case igbinary_type_long8n:
		case igbinary_type_long16n:
		case igbinary_type_long32p:
		case igbinary_type_long32n:
		case igbinary_type_long64p:
		case igbinary_type_long64n:
			if (igbinary_unserialize_long(igsd, t, &tmp_long)) {
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
			if (igbinary_unserialize_double(igsd, t, &tmp_double)) {
				return 1;
			}
			ZVAL_DOUBLE(z, tmp_double);
			break;
		default:
			zend_error(E_WARNING, "igbinary_unserialize_zval: unknown type '%02x', position %zu", t, (size_t)IGB_BUFFER_OFFSET(igsd));
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
