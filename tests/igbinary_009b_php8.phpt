--TEST--
Check for reference serialization in php 8 (Original example, not using var_dump)
--SKIPIF--
<?php
if (!extension_loaded('igbinary')) {
	echo "skip no igbinary";
}
if (PHP_MAJOR_VERSION < 8) {
	echo "skip requires php 8.0+\n";
}
--INI--
pcre.jit=0
--FILE--
<?php
error_reporting(PHP_VERSION_ID < 80400 ?E_ALL|E_STRICT : E_ALL);
// Verify that $type[0] is the same zval as $type[0][0][0], but different from $type[0]
function test_cyclic2($type, $variable) {
	$serialized = igbinary_serialize($variable);
	$unserialized = igbinary_unserialize($serialized);
	echo $type, "\n";
	echo substr(bin2hex($serialized), 8), "\n";
	// Can't use === or == on two recursive arrays in some cases
	echo array_keys($unserialized) === array_keys($variable) && array_keys($unserialized[0]) === array_keys($variable[0]) ? 'OK' : 'ERROR', "\n";
	ob_start();
	var_dump($variable);
	$dump_exp = ob_get_clean();
	ob_start();
	var_dump($unserialized);
	$dump_act = ob_get_clean();
	if (preg_replace('/&array/', 'array', $dump_act) !== preg_replace('/&array/', 'array', $dump_exp)) {
		echo "But var dump differs:\nActual:\n", $dump_act, "\nExpected\n", $dump_exp, "\n";
		echo "(Was normalized)\n";
	}

	if (!isset($unserialized[0]) || count($unserialized) != 1) {
		printf("Unexpected keys: %s\n", array_keys($unserialized));
		return;
	} else if (!is_array($unserialized)) {
		printf("\$a[0] is not an array, it is %s", gettype($unserialized));
		return;
	}
	// Set a key, check for the presence of the key 2 levels deeper (Should find it) and 1 level deeper (Should not find it)
	$unserialized[0]['test'] = 'foo';
	if ($unserialized[0][0][0]['test'] !== 'foo') {
		echo "Expected the unserialized array to be cyclic\n";
	}
	if (isset($unserialized[0][0]['test'])) {
		echo "Expected the unserialized array to be cyclic AND of cycle depth 2, but cycle depth is 1\n";
	}
}
$a = [null];
$b = [&$a];
$a[0] = &$b;
// 1401060025140106002514010600250101 could also be serialized as 14010600251401060014010600250101 if we normalized the references which only occurred once in the serialization
// (Replace middle &array(&$a) with array(&$array), i.e. second 2514 with 14)
test_cyclic2('cyclic $a = array(&array(&$a)) - testing functionality', $a);
unset($a);
$a = null;
$a = [[&$a]];
test_cyclic2('cyclic $a = array(array(&$a)); $a[0] - testing functionality', $a[0]);
// $a serializes as 140106001401060025140106000101 - This is a bug, probably exists in php5 as well.
--EXPECT--
cyclic $a = array(&array(&$a)) - testing functionality
1401060025140106002514010600250101
OK
But var dump differs:
Actual:
array(1) {
  [0]=>
  &array(1) {
    [0]=>
    array(1) {
      [0]=>
      *RECURSION*
    }
  }
}

Expected
array(1) {
  [0]=>
  &array(1) {
    [0]=>
    *RECURSION*
  }
}

(Was normalized)
cyclic $a = array(array(&$a)); $a[0] - testing functionality
14010600251401060014010600250101
OK
But var dump differs:
Actual:
array(1) {
  [0]=>
  &array(1) {
    [0]=>
    array(1) {
      [0]=>
      *RECURSION*
    }
  }
}

Expected
array(1) {
  [0]=>
  &array(1) {
    [0]=>
    *RECURSION*
  }
}

(Was normalized)
