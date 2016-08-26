#include "php_version.h"
#if PHP_MAJOR_VERSION == 5
#include "ext/igbinary/src/php5/php_igbinary.h"
#elif PHP_MAJOR_VERSION == 7
#include "ext/igbinary/src/php7/php_igbinary.h"
#else
#error "Unsupported php version for igbinary build"
#endif
