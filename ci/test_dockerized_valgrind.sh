#!/usr/bin/env bash
# Runs this PECL's unit tests with valgrind and a customizable php build
# TODO: Support overridable PHP_CONFIGURE_ARGS
#
# We reuse the php base image because
# 1. It has any necessary dependencies installed for php 7.0-8.0
# 2. It is already downloaded
#
# We need to install valgrind then rebuild php from source with the configure option '--with-valgrind' to avoid valgrind false positives
# because php-src has inline assembly that causes false positives in valgrind when that option isn't used.
if [[ $# < 2 || $# > 3 ]]; then
    echo "Usage: $0 PHP_VERSION PHP_VERSION_FULL [i386]" 1>&2
    echo "e.g. $0 8.0 8.0.3" 1>&2
    echo "The PHP_VERSION is the version of the php docker image to use" 1>&2
    echo "The PHP_VERSION_FULL is the version of the php release to download using ci/install_php_custom.sh" 1>&2
    exit 1
fi
# -x Exit immediately if any command fails
# -e Echo all commands being executed.
# -u fail for undefined variables
set -xeu
PHP_VERSION=$1
PHP_VERSION_FULL=$2
ARCHITECTURE=${3:-}

case "$PHP_VERSION_FULL" in
    *RC[0-9]*)
        PHP_VERSION="${PHP_VERSION_FULL}"
        ;;
    *alpha[0-9]*)
        PHP_VERSION="${PHP_VERSION_FULL}"
        ;;
    *beta[0-9]*)
        PHP_VERSION="${PHP_VERSION_FULL}"
        ;;
    *)
        # Use only the major.minor version for stable releases.
        # As provided in PHP_VERSION
        ;;
esac

# NOTE: php 7.3-8.0 (but not 8.1) will fail in valgrind without "--with-valgrind" because php-src uses custom assembly for its implementation of zend_string_equals
# In order to fix those false positives, a different set of images would be needed where (1) valgrind was installed before compiling php, and (2) php was compiled with support for valgrind (--with-valgrind) to avoid false positives
# docker run --rm $DOCKER_IMAGE ci/test_inner_valgrind.sh
CFLAGS="-DZEND_RC_DEBUG=1"
PHP_CONFIGURE_ARGS="--disable-all --enable-zts --enable-debug --enable-cgi --enable-session --enable-json"
if [[ "$ARCHITECTURE" == i386 ]]; then
	PHP_IMAGE="$ARCHITECTURE/php"
	DOCKER_IMAGE_VALGRIND="igbinary-valgrind-test-runner:$ARCHITECTURE-$PHP_VERSION_FULL"
	# php infers the x86_64 platform and php fails to build the corresponding assembly.
	# igbinary tests don't even involve Fibers
	PHP_CONFIGURE_ARGS="$PHP_CONFIGURE_ARGS --disable-fiber-asm"
	CFLAGS="$CFLAGS -m32"
else
	PHP_IMAGE="php"
	DOCKER_IMAGE_VALGRIND="igbinary-valgrind-test-runner:$ARCHITECTURE-$PHP_VERSION"
fi

DOCKER_IMAGE_VALGRIND=igbinary-valgrind-test-runner:$PHP_VERSION_FULL
docker build \
	--build-arg="CFLAGS=$CFLAGS" \
	--build-arg="PHP_CONFIGURE_ARGS=$PHP_CONFIGURE_ARGS" \
	--build-arg="PHP_IMAGE=$PHP_IMAGE" \
	--build-arg="PHP_VERSION=$PHP_VERSION" \
	--build-arg="PHP_VERSION_FULL=$PHP_VERSION_FULL" \
	--tag="$DOCKER_IMAGE_VALGRIND" \
	-f ci/Dockerfile.valgrind .
docker run --rm $DOCKER_IMAGE_VALGRIND ci/test_inner_valgrind.sh
