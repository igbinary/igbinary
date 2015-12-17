--TEST--
Check for array+string serialization
--SKIPIF--
--INI--
igbinary.compact_strings=1
--FILE--
<?php 
if(!extension_loaded('igbinary')) {
	dl('igbinary.' . PHP_SHLIB_SUFFIX);
}

function test($type, $variable) {
	$serialized = igbinary_serialize($variable);
	$unserialized = igbinary_unserialize($serialized);

	echo $type, "\n";
	echo substr(bin2hex($serialized), 8), "\n";
	if ($unserialized != $variable) {
		echo 'ERROR, expected: ';
		var_dump($variable);
		echo 'got: ';
		var_dump($unserialized);
	} else {
		echo 'OK';
	}
	echo "\n";
}

test('array("foo", "foo", "foo")', array("foo", "foo", "foo"));
test('array("one" => 1, "two" => 2))', array("one" => 1, "two" => 2));
test('array("kek" => "lol", "lol" => "kek")', array("kek" => "lol", "lol" => "kek"));
test('array("" => "empty")', array("" => "empty"));

?>
--EXPECT--
array("foo", "foo", "foo")
140306001103666f6f06010e0006020e00
OK
array("one" => 1, "two" => 2))
140211036f6e650601110374776f0602
OK
array("kek" => "lol", "lol" => "kek")
140211036b656b11036c6f6c0e010e00
OK
array("" => "empty")
14010d1105656d707479
OK
