#!/bin/sh

phpize
CFLAGS="-O3" ./configure --enable-igbinary
make clean all

rm -rf debian

php_ext_dir=$(php-config --extension-dir)

mkdir debian
mkdir -p debian/DEBIAN
mkdir -p debian/${php_ext_dir}
mkdir -p debian/etc/php5/conf.d

cp debian.control debian/DEBIAN/control
cp igbinary.php.ini debian/etc/php5/conf.d/igbinary.ini
cp modules/igbinary.so debian/${php_ext_dir}

dpkg -b debian igbinary-$(dpkg --print-architecture).deb

rm -rf debian

