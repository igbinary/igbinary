/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Author: Oleg Grenrus <oleg.grenrus@dynamoid.com>                     |
  | See CREDITS for contributors                                         |
  +----------------------------------------------------------------------+
*/

#ifndef IGBINARY_H
#define IGBINARY_H
#ifdef PHP_WIN32
# include "win32/php_stdint.h"
#else
# include <stdint.h>
#endif

/* Forward declarations. */
struct zval;

/* Constants and constant macros */
/** Binary protocol version of igbinary. */
#define IGBINARY_FORMAT_VERSION 0x00000002

#define PHP_IGBINARY_VERSION "2.0.0"

/* Macros */

#ifdef PHP_WIN32
#	if defined(IGBINARY_EXPORTS) || (!defined(COMPILE_DL_IGBINARY))
#		define IGBINARY_API __declspec(dllexport)
#	elif defined(COMPILE_DL_IGBINARY)
#		define IGBINARY_API __declspec(dllimport)
#	else
#		define IGBINARY_API /* nothing special */
#	endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define IGBINARY_API __attribute__ ((visibility("default")))
#else
#	define IGBINARY_API /* nothing special */
#endif

/** Struct that contains pointers to memory allocation and deallocation functions.
 * @see igbinary_serialize_data
 */
struct igbinary_memory_manager {
	void *(*alloc)(size_t size, void *context);
	void *(*realloc)(void *ptr, size_t new_size, void *context);
	void (*free)(void *ptr, void *context);
	void *context;
};

/** Serialize zval.
 * Return buffer is allocated by this function with emalloc.
 * @param[out] ret Return buffer
 * @param[out] ret_len Size of return buffer
 * @param[in] z Variable to be serialized
 * @return 0 on success, 1 elsewhere.
 */
IGBINARY_API int igbinary_serialize(uint8_t **ret, size_t *ret_len, zval *z TSRMLS_DC);

/** Serialize zval.
 * Return buffer is allocated by this function with emalloc.
 * @param[out] ret Return buffer
 * @param[out] ret_len Size of return buffer
 * @param[in] z Variable to be serialized
 * @param[in] memory_manager Pointer to the structure that contains memory allocation functions.
 * @return 0 on success, 1 elsewhere.
 */
IGBINARY_API int igbinary_serialize_ex(uint8_t **ret, size_t *ret_len, zval *z, struct igbinary_memory_manager *memory_manager TSRMLS_DC);

/** Unserialize to zval.
 * @param[in] buf Buffer with serialized data.
 * @param[in] buf_len Buffer length.
 * @param[out] z Unserialized zval
 * @return 0 on success, 1 elsewhere.
 */
IGBINARY_API int igbinary_unserialize(const uint8_t *buf, size_t buf_len, zval *z TSRMLS_DC);

#endif /* IGBINARY_H */
