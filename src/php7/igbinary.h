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
#include <stdint.h>

/* Forward declarations. */
struct zval;

/* Constants and constant macros */
/** Binary protocol version of igbinary. */
#define IGBINARY_FORMAT_VERSION 0x00000002

#define PHP_IGBINARY_VERSION "3.2.17"

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
IGBINARY_API int igbinary_serialize(uint8_t **ret, size_t *ret_len, zval *z);

/** Serialize zval.
 * Return buffer is allocated by this function with emalloc.
 * @param[out] ret Return buffer
 * @param[out] ret_len Size of return buffer
 * @param[in] z Variable to be serialized
 * @param[in] memory_manager Pointer to the structure that contains memory allocation functions.
 * @return 0 on success, 1 elsewhere.
 */
IGBINARY_API int igbinary_serialize_ex(uint8_t **ret, size_t *ret_len, zval *z, struct igbinary_memory_manager *memory_manager);

/** Unserialize to zval.
 * @param[in] buf Buffer with serialized data.
 * @param[in] buf_len Buffer length.
 * @param[out] z Unserialized zval
 * @return 0 on success, 1 elsewhere.
 */
IGBINARY_API int igbinary_unserialize(const uint8_t *buf, size_t buf_len, zval *z);

static zend_always_inline int _igbinary_has_valid_header(const uint8_t *buf, size_t buf_len) {
	if (buf_len < 5) {
		/* Must have 4 header bytes and at least one byte of data */
		return 0;
	}
	/* Unserialize 32bit value the same way on big-endian and little-endian architectures.
	 * This compiles to a load+optional bswap when compiler optimizations are enabled. */
	const uint32_t ret =
	    ((uint32_t)(buf[0]) << 24) |
	    ((uint32_t)(buf[1]) << 16) |
	    ((uint32_t)(buf[2]) << 8) |
	    ((uint32_t)(buf[3]));
	return ret == 1 || ret == 2;
}
/** This is defined as a macro and a static C function
 * to allow callers to use the macro from newer igbinary versions even with older igbinary installations. */
#define igbinary_has_valid_header(buf, buf_len) _igbinary_has_valid_header((buf), (buf_len))


#endif /* IGBINARY_H */
