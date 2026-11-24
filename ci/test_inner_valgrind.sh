#!/usr/bin/env bash
# -x Exit immediately if any command fails
# -e Echo all commands being executed.
# -u fail for undefined variables
set -xeu
echo "Run tests in docker"
php --version
php --ini
#cp ci/run-tests-parallel.php run-tests.php
#REPORT_EXIT_STATUS=1 make test TESTS="-j$(nproc) -P -q --show-diff -m --show-mem"
case "$PHP_VERSION" in
    7.0.*|7.1.*|7.2.*|7.3.*)
        TEST_ARGS_SHOW_MEM=""
        ;;
    *)
        TEST_ARGS_SHOW_MEM=" --show-mem "
        ;;
esac
REPORT_EXIT_STATUS=1 make test TESTS="-P -q --show-diff -m $TEST_ARGS_SHOW_MEM"
