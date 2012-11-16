--TEST--
Cyclic array test 2
--INI--
report_memleaks=0
--SKIPIF--
<?php
if(!extension_loaded('igbinary')) {
	echo "skip no igbinary";
}

if (version_compare(PHP_VERSION, "5.2.16", "<")) {
	echo "skip only test on php version 5.2.16 and above";
}

--FILE--
<?php

$a = array("foo" => &$b);
$b = array(1, 2, $a);

/* all three statements below should produce same output however PHP stock
 * unserialize/serialize produces different output (5.2.16). I consider this is
 * a PHP bug. - Oleg Grenrus
 */

//$k = $a;
//$k = unserialize(serialize($a));
$k = igbinary_unserialize(igbinary_serialize($a));

function check($a, $k) {
	$a_str = print_r($a, true);
	$k_str = print_r($k, true);

	if ($a_str !== $k_str) {
		echo "Output differs\n";
		echo "Expected:\n", $a_str, "\n";
		echo "Actual:\n", $k_str, "\n";
	} else {
		echo "OK\n";
	}
}

check($a, $k);

$a["foo"][2]["foo"][1] = "b";
$k["foo"][2]["foo"][1] = "b";

check($a, $k);

?>
--EXPECT--
OK
OK
