/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  +----------------------------------------------------------------------+
  | Author: Oleg Grenrus <oleg.grenrus@dynamoid.com>                     |
  | See CREDITS for contributors                                         |
  +----------------------------------------------------------------------+
*/

#ifndef PHP_IGBINARY_H
#define PHP_IGBINARY_H

/** Module entry of igbinary. */
extern zend_module_entry igbinary_module_entry;
#define phpext_igbinary_ptr &igbinary_module_entry

#ifdef PHP_WIN32
#define PHP_IGBINARY_API __declspec(dllexport)
#else
#define PHP_IGBINARY_API
#endif

ZEND_BEGIN_MODULE_GLOBALS(igbinary)
	zend_bool compact_strings;
ZEND_END_MODULE_GLOBALS(igbinary)

#ifdef ZTS
#include "TSRM.h"
#endif

#include "ext/standard/php_smart_string.h"

/** Module init function. */
PHP_MINIT_FUNCTION(igbinary);

/** Module shutdown function. */
PHP_MSHUTDOWN_FUNCTION(igbinary);

/** Request init function. */
PHP_RINIT_FUNCTION(igbinary);

/** Request shutdown function. */
PHP_RSHUTDOWN_FUNCTION(igbinary);

/** Module info function for phpinfo(). */
PHP_MINFO_FUNCTION(igbinary);

/** string igbinary_serialize(mixed value).
 * Returns the binary serialized value.
 */
PHP_FUNCTION(igbinary_serialize);

/** mixed igbinary_unserialize(string data).
 * Unserializes the given inputstring (value).
 */
PHP_FUNCTION(igbinary_unserialize);

#ifdef ZTS
#define IGBINARY_G(v) TSRMG(igbinary_globals_id, zend_igbinary_globals *, v)
#else
#define IGBINARY_G(v) (igbinary_globals.v)
#endif

/** Binary protocol version of igbinary. */
#define IGBINARY_FORMAT_VERSION 0x00000002

#endif /* PHP_IGBINARY_H */


/*
 * Local variables:
 * tab-width: 2
 * c-basic-offset: 0
 * End:
 * vim600: noet sw=2 ts=2 fdm=marker
 * vim<600: noet sw=2 ts=2
 */
