--TEST--
Object test, __wakeup (With multiple references)
--SKIPIF--
<?php
if (PHP_VERSION_ID >= 80500) {
    echo "skip: __sleep() and __wakeup() are deprecated in PHP 8.5+ (see https://wiki.php.net/rfc/deprecations_php_8_5#deprecate_the_sleep_and_wakeup_magic_methods)";
}
?>
--FILE--
<?php
if(!extension_loaded('igbinary')) {
	dl('igbinary.' . PHP_SHLIB_SUFFIX);
}

class Obj {
	var $a;
	var $b;

	function __construct($a, $b) {
		$this->a = $a;
		$this->b = $b;
	}

	function __wakeup() {
		$this->b = $this->a * 3;
	}
}

function main() {
	$o = new Obj(1, 2);
	$variable = array(&$o, &$o);
	$serialized = igbinary_serialize($variable);
	$unserialized = igbinary_unserialize($serialized);

	echo substr(bin2hex($serialized), 8), "\n";
	echo $unserialized[0]->b === 3 && $unserialized[0]->a === 1 ? 'OK' : 'ERROR';
	echo "\n";
	$unserialized[0] = 'a';
	var_dump($unserialized);
}

main();
--EXPECT--
140206002517034f626a1402110161060111016206020601252201
OK
array(2) {
  [0]=>
  &string(1) "a"
  [1]=>
  &string(1) "a"
}
