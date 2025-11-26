#!/usr/bin/env bash
if [[ $# < 1 || $# > 3 ]]; then
    echo "Usage: $0 PHP_VERSION PHP_VERSION_FULL [i386]" 1>&2
    echo "e.g. $0 8.0 8.0.10 i386" 1>&2
    echo "The PHP_VERSION is the version of the php docker image to use" 1>&2
    exit 1
fi
# -x Exit immediately if any command fails
# -e Echo all commands being executed.
# -u fail for undefined variables
set -xeu
PHP_VERSION=$1
PHP_VERSION_FULL=${2:-$PHP_VERSION}
ARCHITECTURE=${3:-}

# Determine if we have a pre-release version, and if so, use the full version
# instead of the short version. Docker hub only has short tags for stable
# releases.
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

if [[ "$ARCHITECTURE" == i386 ]]; then
	PHP_IMAGE="$ARCHITECTURE/php"
	DOCKER_IMAGE="igbinary-test-runner:$ARCHITECTURE-$PHP_VERSION"
else
	PHP_IMAGE="php"
	DOCKER_IMAGE="igbinary-test-runner:$PHP_VERSION"
fi

docker build --build-arg="PHP_VERSION=$PHP_VERSION" --build-arg="PHP_IMAGE=$PHP_IMAGE" --tag="$DOCKER_IMAGE" -f ci/Dockerfile .
docker run --rm $DOCKER_IMAGE ci/test_inner.sh
