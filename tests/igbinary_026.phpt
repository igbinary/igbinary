--TEST--
Cyclic array test
--INI--
report_memleaks=0
igbinary.compact_strings=1
--SKIPIF--
<?php
if(!extension_loaded('igbinary')) {
	echo "skip no igbinary";
}
--FILE--
<?php 

function test($type, $variable, $test) {
	$serialized = igbinary_serialize($variable);
	$unserialized = igbinary_unserialize($serialized);

	echo $type, "\n";
	echo substr(bin2hex($serialized), 8), "\n";
	echo !$test || $unserialized == $variable ? 'OK' : 'ERROR', "\n";
}

$a = array(
	'a' => array(
		'b' => 'c',
		'd' => 'e'
	),
);

$a['f'] = &$a;

test('array', $a, false);

$a = array("foo" => &$b);
$b = array(1, 2, $a);

$exp = $a;
$act = igbinary_unserialize(igbinary_serialize($a));

$dump_exp = print_r($exp, true);
$dump_act = print_r($act, true);

if ($dump_act !== $dump_exp) {
	echo "Var dump differs:\n", $dump_act, "\n", $dump_exp, "\n";
} else {
	echo "Var dump OK\n";
}

$act['foo'][1] = 'test value';
$exp['foo'][1] = 'test value';
if ($act['foo'][1] !== $act['foo'][2]['foo'][1]) {
	echo "Recursive elements differ:\n";
	var_dump($act);
	var_dump($act['foo']);
	var_dump($exp);
	var_dump($exp['foo']);
}

?>
--EXPECT--
array
140211016114021101621101631101641101651101662514020e0001010e05250102
OK
Var dump OK
