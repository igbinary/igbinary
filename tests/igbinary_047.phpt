--TEST--
Zlib compression support
--SKIPIF--
<?php
if (!extension_loaded('zlib')) {
	echo "skip zlib not loaded";
}
--INI--
igbinary.compression = On
igbinary.compression_min_size = 128
igbinary.compression_level = 5
--FILE--
<?php 
if(!extension_loaded('igbinary')) {
	dl('igbinary.' . PHP_SHLIB_SUFFIX);
}

function test($type, $variable, $test) {
	$serialized = igbinary_serialize($variable);
	$unserialized = igbinary_unserialize($serialized);

	echo $type, "\n";
	echo substr(bin2hex($serialized), 8), "\n";
	echo $test || $unserialized == $variable ? 'OK' : 'ERROR';
	echo "\n";
}

$a = array();

for ($x = 0; $x < 20; $x++) {
	$a[] = 'zzz' . $x;
}

test('array', $a, false);

?>
--EXPECT--
array
000000b0785e25cc471682400004d19150824a18e120e470b539bdcfee55fd558510b27124c422a534f1506732752157570a75a35477500f9eea49a55ed4eacd2b96ffcfc4db98f9180b8db1d21a1b9db1d31b07d138f91a178371ff00609a25ab
OK

