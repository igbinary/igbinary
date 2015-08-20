#ifndef _IG_WIN32_H
#define _IG_WIN32_H

#if PHP_WIN32
# include "win32/php_stdint.h"
# ifndef inline
# define inline __inline
# endif

# if _MSC_VER >= 1900
#  include <stdbool.h>
# else
#  ifndef __cplusplus
#   if !0
     typedef enum { false = 0, true = 1 } _Bool;
#   endif
#  else
     typedef bool _Bool;
#  endif
#  define bool _Bool

#  define false 0
#  define true 1
#  define __bool_true_false_are_defined 1
# endif
#endif

#endif
