dnl config.m4 for extension igbinary

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

dnl PHP_ARG_WITH(igbinary, for igbinary support,
dnl Make sure that the comment is aligned:
dnl [  --with-igbinary             Include igbinary support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(igbinary, whether to enable igbinary support,
  [  --enable-igbinary          Enable igbinary support])

if test "$PHP_IGBINARY" != "no"; then
  AC_CHECK_HEADERS([stdbool.h],, AC_MSG_ERROR([stdbool.h not exists]))
  AC_CHECK_HEADERS([stddef.h],, AC_MSG_ERROR([stddef.h not exists]))
  AC_CHECK_HEADERS([stdint.h],, AC_MSG_ERROR([stdint.h not exists]))

dnl  APC and APCu do not support PHP 7 yet, until we know the API I don't
dnl  think there is anything we can do --Sean-Der
dnl
dnl  AC_MSG_CHECKING([for APC/APCU includes])
dnl  if test -f "$phpincludedir/ext/apcu/apc_serializer.h"; then
dnl    apc_inc_path="$phpincludedir"
dnl    AC_MSG_RESULT([APCU in $apc_inc_path])
dnl    AC_DEFINE(HAVE_APCU_SUPPORT,1,[Whether to enable apcu support])
dnl  elif test -f "$phpincludedir/ext/apc/apc_serializer.h"; then
dnl    apc_inc_path="$phpincludedir"
dnl    AC_MSG_RESULT([APC in $apc_inc_path])
dnl    AC_DEFINE(HAVE_APC_SUPPORT,1,[Whether to enable apc support])
dnl  elif test -f "${srcdir}/apc_serializer.h"; then
dnl    AC_MSG_RESULT([apc_serializer.h bundled])
dnl    AC_DEFINE(HAVE_APC_SUPPORT,1,[Whether to enable apc support])
dnl    AC_DEFINE(USE_BUNDLED_APC,1,[Whether to use bundled apc includes])
dnl  else
dnl    AC_MSG_RESULT([not found])
dnl  fi

  AC_CHECK_SIZEOF([long])

  dnl GCC
  AC_MSG_CHECKING(compiler type)
  if test ! -z "`$CC --version | grep -i GCC`"; then
    AC_MSG_RESULT(gcc)
    if test -z "`echo $CFLAGS | grep -- -O0`"; then
      PHP_IGBINARY_CFLAGS="$CFLAGS -Wall -Wpointer-arith -Wmissing-prototypes -Wstrict-prototypes -Wcast-align -Wshadow -Wwrite-strings -Wswitch -Winline -finline-limit=10000 --param large-function-growth=10000 --param inline-unit-growth=10000"
    fi
  elif test ! -z "`$CC --version | grep -i ICC`"; then
    AC_MSG_RESULT(icc)
    if test -z "`echo $CFLAGS | grep -- -O0`"; then
      PHP_IGBINARY_CFLAGS="$CFLAGS -no-prec-div -O3 -x0 -unroll2"
    fi
  elif test ! -z "`$CC --version | grep -i CLANG`"; then
    AC_MSG_RESULT(clang)
    if test -z "`echo $CFLAGS | grep -- -O0`"; then
      PHP_IGBINARY_CFLAGS="$CFLAGS -Wall -O2"
    fi
  else
    AC_MSG_RESULT(other)
  fi

  PHP_ADD_MAKEFILE_FRAGMENT(Makefile.bench)
  PHP_INSTALL_HEADERS([ext/igbinary], [igbinary.h])
  PHP_NEW_EXTENSION(igbinary, igbinary.c hash_si.c, $ext_shared,, $PHP_IGBINARY_CFLAGS)
  PHP_ADD_EXTENSION_DEP(igbinary, session, true)
  PHP_SUBST(IGBINARY_SHARED_LIBADD)
fi
