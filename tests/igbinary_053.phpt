--TEST--
__wakeup can modify properties without affecting other objects
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
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
