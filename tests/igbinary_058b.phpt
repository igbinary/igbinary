--TEST--
Should not call __destruct if __wakeup throws an exception (in arrays)
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
			throw new Exception("__wakeup threw for id " . $this->id);
		}
	}

	public function __destruct() {
		printf("Calling __destruct %s dynamic=%s\n", $this->id, $this->dynamic);
	}
}
function main() {
	$values = [
		0      => new Thrower("a", false),
		'foo'  => 'last',
		'key1' => new Thrower("b", false),
		2      => new Thrower("c", true),
		'last' => new Thrower("d", false),
	];
	$serialized = igbinary_serialize($values);
	$values = null;
    printf("Going to unserialize\n");
	try {
		igbinary_unserialize($serialized);
	} catch (Exception $e) {
		printf("Caught %s\n", $e->getMessage());
	}
}
main();
--EXPECT--
Calling __destruct a dynamic=original
Calling __destruct b dynamic=original
Calling __destruct c dynamic=original
Calling __destruct d dynamic=original
Going to unserialize
Calling __wakeup a
Calling __wakeup b
Calling __wakeup c
Calling __destruct a dynamic=copy
Calling __destruct b dynamic=copy
Caught __wakeup threw for id c
