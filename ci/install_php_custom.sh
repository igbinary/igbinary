#!/bin/bash -xeu

# Required env variables:
#  PHP_CONFIGURE_ARGS
#  PHP_CUSTOM_VERSION (E.g. 7.4.20)
#  PHP_INSTALL_DIR (path to installation directory)

# optional env vars:
#  CFLAGS
#  CC
#  PATH
echo "Attempting to install PHP from source, PHP version '$PHP_CUSTOM_VERSION'/ configure args '$PHP_CONFIGURE_ARGS'"
if [ "x$PHP_CUSTOM_VERSION" = "x" -o "x$PHP_CONFIGURE_ARGS" = "x" ] ; then
	echo "Missing php version or configuration arguments";
	exit 1;
fi
PHP_FOLDER="php-$PHP_CUSTOM_VERSION"
PHP_INSTALL_DIR="$(./ci/generate_php_install_dir.sh)"
echo "Downloading $PHP_INSTALL_DIR\n"
if [ -x $PHP_INSTALL_DIR/bin/php ] ; then
	echo "PHP $PHP_CUSTOM_VERSION already installed and in cache at $PHP_INSTALL_DIR";
	exit 0
fi
PHP_CUSTOM_NORMAL_VERSION=${PHP_CUSTOM_VERSION//RC[0-9]/}
PHP_CUSTOM_NORMAL_VERSION=${PHP_CUSTOM_NORMAL_VERSION//alpha[0-9]/}
PHP_CUSTOM_NORMAL_VERSION=${PHP_CUSTOM_NORMAL_VERSION//beta[0-9]/}

# Remove cache if it somehow exists
if [ "x${TRAVIS:-0}" != "x" ]; then
    rm -rf $HOME/travis_cache/
fi

# Otherwise, put a minimal installation inside of the cache.
if [ "$PHP_CUSTOM_NORMAL_VERSION" == "8.5.0" ] ; then
	PHP_CUSTOM_VERSION=8.5.0RC1
	PHP_FOLDER="php-$PHP_CUSTOM_VERSION"

	PHP_TAR_FILE="$PHP_FOLDER.tar.bz2"
	PHP_TAR_URL=https://downloads.php.net/~daniels/$PHP_TAR_FILE
else
	PHP_FOLDER="php-$PHP_CUSTOM_VERSION"

	PHP_TAR_FILE="$PHP_FOLDER.tar.bz2"
	PHP_TAR_URL=https://secure.php.net/distributions/$PHP_TAR_FILE
fi
if [ ! -f $PHP_TAR_FILE ]; then
	curl --location --verbose $PHP_TAR_URL -o $PHP_TAR_FILE
fi

tar xjf $PHP_TAR_FILE

pushd $PHP_FOLDER
./configure $PHP_CONFIGURE_ARGS --prefix="$PHP_INSTALL_DIR"
make -j5
make install
popd

echo "PHP $PHP_CUSTOM_VERSION successfully installed and in cache at $HOME/travis_cache/$PHP_FOLDER";
