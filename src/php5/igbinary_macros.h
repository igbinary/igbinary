/*
  +----------------------------------------------------------------------+
  | See COPYING file for further copyright information                   |
  +----------------------------------------------------------------------+
  | See CREDITS for contributors                                         |
  +----------------------------------------------------------------------+
*/
#ifndef PHP_IGBINARY_MACROS_H
#define PHP_IGBINARY_MACROS_H

// PHP 5.2 doesn't define EXPECTED or UNEXPECTED in Zend/zend.h.
#ifndef EXPECTED
# define EXPECTED(expr) (expr)
#endif
#ifndef UNEXPECTED
# define UNEXPECTED(expr) (expr)
#endif

#endif
