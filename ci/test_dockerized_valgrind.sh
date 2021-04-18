#!/usr/bin/env bash
# Runs this PECL's unit tests with valgrind and a customizable php build
# TODO: Support 32-bit builds
# TODO: Support overridable PHP_CONFIGURE_ARGS
#
# We reuse the php base image because
# 1. It has any necessary dependencies installed for php 7.0-8.0
# 2. It is already downloaded
#
# We need to install valgrind then rebuild php from source with the configure option '--with-valgrind' to avoid valgrind false positives
# because php-src has inline assembly that causes false positives in valgrind when that option isn't used.
if [ $# != 2 ]; then
    echo "Usage: $0 PHP_VERSION PHP_VERSION_FULL" 1>&2
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

# NOTE: php 7.3+ will fail in valgrind without "--with-valgrind" because php-src uses custom assembly for its implementation of zend_string_equals
# In order to fix those false positives, a different set of images would be needed where (1) valgrind was installed before compiling php, and (2) php was compiled with support for valgrind (--with-valgrind) to avoid false positives
# docker run --rm $DOCKER_IMAGE ci/test_inner_valgrind.sh
DOCKER_IMAGE_VALGRIND=igbinary-$PHP_VERSION_FULL-valgrind-test-runner
docker build --build-arg="PHP_VERSION=$PHP_VERSION" --build-arg="PHP_VERSION_FULL=$PHP_VERSION_FULL" --tag="$DOCKER_IMAGE_VALGRIND" -f ci/Dockerfile.valgrind .
docker run --rm $DOCKER_IMAGE_VALGRIND ci/test_inner_valgrind.sh
