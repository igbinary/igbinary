--TEST--
Should not call __destruct if __wakeup throws an exception
--INI--
igbinary.compact_strings = On
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
class Thrower {
	public $id;
	public $throws;
	public $dynamic;
	public function __construct($id, $throws = false) {
		$this->id = $id;
		$this->throws = $throws;
		$this->dynamic = "original";
	}
	public function __wakeup() {
		printf("Calling __wakeup %s\n", $this->id);
		$this->dynamic = "copy";
		if ($this->throws) {
			throw new Exception("__wakeup threw");
		}
	}

	public function __destruct() {
		printf("Calling __destruct %s dynamic=%s\n", $this->id, $this->dynamic);
	}
}
function main() {
	$a = new Thrower("a", true);
	$serialized = igbinary_serialize($a);
	try {
		igbinary_unserialize($serialized);
	} catch (Exception $e) {
		printf("Caught %s\n", $e->getMessage());
	}
	$a = null;
	print("Done a\n");
	$b = new Thrower("b", false);
	$serialized = igbinary_serialize($b);
	$bCopy = igbinary_unserialize($serialized);
	print("Unserialized b\n");
	var_dump($bCopy);

	$bCopy = null;
	$b = null;
	print("Done b\n");
}
main();
--EXPECT--
Calling __wakeup a
Caught __wakeup threw
Calling __destruct a dynamic=original
Done a
Calling __wakeup b
Unserialized b
object(Thrower)#2 (3) {
  ["id"]=>
  string(1) "b"
  ["throws"]=>
  bool(false)
  ["dynamic"]=>
  string(4) "copy"
}
Calling __destruct b dynamic=copy
Calling __destruct b dynamic=original
Done b
