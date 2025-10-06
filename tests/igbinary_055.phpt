--TEST--
__wakeup can replace a copy of the object referring to the root node.
--SKIPIF--
<?php
if (PHP_VERSION_ID >= 80500) {
    echo "skip: __sleep() and __wakeup() are deprecated in PHP 8.5+ (see https://wiki.php.net/rfc/deprecations_php_8_5#deprecate_the_sleep_and_wakeup_magic_methods)";
}
?>
--FILE--
<?php

#[AllowDynamicProperties]
class Obj {
	function __construct($a) {
		$this->a = $a;
	}

	public function __wakeup() {
		echo "Calling __wakeup\n";
		$this->a = "replaced";
	}
}

$a = new stdClass();
$a->obj = new Obj($a);;
$serialized = igbinary_serialize($a);
printf("%s\n", bin2hex($serialized));
$unserialized = igbinary_unserialize($serialized);
var_dump($unserialized);
--EXPECTF--
000000021708737464436c617373140111036f626a17034f626a14011101612200
Calling __wakeup
object(stdClass)#%d (1) {
  ["obj"]=>
  object(Obj)#%d (1) {
    ["a"]=>
    string(8) "replaced"
  }
}
