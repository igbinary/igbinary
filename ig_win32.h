#ifndef _IG_WIN32_H
#define _IG_WIN32_H

#if PHP_WIN32
# include "win32/php_stdint.h"
# ifndef inline
# define inline __inline
# endif

# ifndef __cplusplus
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
  typedef enum {false = 0, true = 1} bool;
#endif
# endif

# define false 0
# define true 1
# define __bool_true_false_are_defined 1
#endif

#endif
