#!/usr/bin/env bash
# -x Exit immediately if any command fails
# -e Echo all commands being executed.
# -u fail for undefined variables
set -xeu
echo "Run tests in docker"
php --version
php --ini
cp ci/run-tests-parallel.php run-tests.php
REPORT_EXIT_STATUS=1 make test TESTS="-j$(nproc) -P -q --show-diff -m --show-mem"
echo "Test that package.xml is valid"
pecl package
