#!/bin/bash -xeu

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
# Remove cache if it somehow exists
if [ "x${TRAVIS:-0}" != "x" ]; then
	rm -rf $HOME/travis_cache/
fi
# Otherwise, put a minimal installation inside of the cache.
PHP_TAR_FILE="$PHP_FOLDER.tar.bz2"
curl --verbose https://secure.php.net/distributions/$PHP_TAR_FILE -o $PHP_TAR_FILE
# If we decide to support 7.1.0, it would be `curl --verbose https://downloads.php.net/~davey/php-7.1.0RC3.tar.bz2 -o $PHP_TAR_FILE`
tar xjf $PHP_TAR_FILE

pushd $PHP_FOLDER
./configure $PHP_CONFIGURE_ARGS --prefix="$PHP_INSTALL_DIR"
make -j5
make install
popd

echo "PHP $PHP_CUSTOM_VERSION successfully installed and in cache at $HOME/travis_cache/$PHP_FOLDER";
