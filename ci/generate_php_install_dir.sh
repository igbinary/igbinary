#!/bin/bash -xeu
# Print a folder name to stdout based on the desired integer width(32 bit or 64 bit) and PHP_CONFIGURE_ARGS.
# This could instead be done by the `source` builtin, but wouldn't work if a different shell (e.g. zsh) was calling bash.
PHP_INSTALL_DIR="$HOME/travis_cache/php-$PHP_CUSTOM_VERSION"
if [ "${USE_32BIT:-0}" = "1" ]; then
	PHP_INSTALL_DIR="$PHP_INSTALL_DIR-32bit"
else
	PHP_INSTALL_DIR="$PHP_INSTALL_DIR-64bit"
fi
# Generate a unique 4-character suffix based on $PHP_CONFIGURE_ARGS.
HASH=$(echo -n "$PHP_CONFIGURE_ARGS" | sha1sum | cut -c -4)
PHP_INSTALL_DIR="$PHP_INSTALL_DIR-$HASH"
echo -n "$PHP_INSTALL_DIR"
