#!/bin/bash -xeu

# Adjust Debian APT sources to use a different mirror if needed.
# This is useful in CI environments where the default mirror may be slow or unreliable.

. /etc/os-release

if [ "x${ID:-linux}" != "xdebian" ]; then
    exit 0
fi

mirror=http://cdn-fastly.deb.debian.org
archived=false
if [[ "${VERSION_ID:-}" =~ ^[0-9]+$ ]] && [ "$VERSION_ID" -le 11 ]; then
    mirror=http://archive.debian.org
    archived=true
fi

for sources in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
    [ -f "$sources" ] || continue
    sed -i -E "s#https?://(deb\\.debian\\.org|security\\.debian\\.org|cdn-fastly\\.deb\\.debian\\.org)/debian#$mirror/debian#g" "$sources"
    if $archived && [[ "$sources" == *.list ]]; then
        sed -i 's|^deb .*archive\.debian\.org/debian[[:space:]].*-updates|# &|g' "$sources"
    fi
done

if $archived; then
    echo 'Acquire::Check-Valid-Until "false";' >> /etc/apt/apt.conf.d/99no-check-valid-until
fi

if [ "${VERSION_ID:-}" = 9 ]; then
    echo 'Acquire::AllowInsecureRepositories "true";' >> /etc/apt/apt.conf.d/99no-check-valid-until
    for sources in /etc/apt/sources.list /etc/apt/sources.list.d/*.list; do
        [ -f "$sources" ] || continue
        sed -i 's|^deb .*stretch/updates|# &|g' "$sources"
    done
fi
