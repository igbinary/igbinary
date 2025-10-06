--TEST--
__wakeup can modify properties without affecting other objects
--SKIPIF--
<?php
if (PHP_VERSION_ID >= 80500) {
    echo "skip: __sleep() and __wakeup() are deprecated in PHP 8.5+ (see https://wiki.php.net/rfc/deprecations_php_8_5#deprecate_the_sleep_and_wakeup_magic_methods)";
}
?>
--FILE--
<?php

class Obj {
	private static $count = 1;

	public $a;

	function __construct($a) {
		$this->a = $a;
	}

	public function __wakeup() {
		echo "call wakeup\n";
		$this->a[] = "end";
	}
}

function main() {
	$array = ["test"];  // array (not a reference, but should be copied on write)
	$a = new Obj($array);
	$b = new Obj($array);
	$variable = [$a, $b];
	$serialized = igbinary_serialize($variable);
	printf("%s\n", bin2hex($serialized));
	$unserialized = igbinary_unserialize($serialized);
	var_dump($unserialized);
}
main();
--EXPECTF--
000000021402060017034f626a14011101611401060011047465737406011a0014010e010102
call wakeup
call wakeup
array(2) {
  [0]=>
  object(Obj)#%d (1) {
    ["a"]=>
    array(2) {
      [0]=>
      string(4) "test"
      [1]=>
      string(3) "end"
    }
  }
  [1]=>
  object(Obj)#%d (1) {
    ["a"]=>
    array(2) {
      [0]=>
      string(4) "test"
      [1]=>
      string(3) "end"
    }
  }
}
