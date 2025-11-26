--TEST--
Object test, array of small objects with __sleep
--SKIPIF--
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
if(!extension_loaded('igbinary')) {
	dl('igbinary.' . PHP_SHLIB_SUFFIX);
}

function test($type, $variable, $test) {
	$serialized = igbinary_serialize($variable);
	$unserialized = igbinary_unserialize($serialized);

	var_dump($variable);
	var_dump($unserialized);
}

class Obj {
	private $c;

	function __construct($c) {
		$this->c = $c;
	}

	function __sleep() {
		return array('c');
	}
}

$obj = new Obj(4);

test('array', $obj, true);

?>
--EXPECT--
object(Obj)#1 (1) {
  ["c":"Obj":private]=>
  int(4)
}
object(Obj)#2 (1) {
  ["c":"Obj":private]=>
  int(4)
}
