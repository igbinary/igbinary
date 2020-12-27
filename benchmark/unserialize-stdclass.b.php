<?php

// Description: Serialize stdClass with dynamic values

require_once 'bench.php';

$b = new Bench('serialize-stdclass');

$array = [];
for ($i = 0; $i < 1000; $i++) {
	$o = new stdClass();
	$o->foo = 10;
	$o->i = $i;
	$o->isOdd = $i % 2 != 0;
	$array[] = $o;
}
$ser = igbinary_serialize($array);

for ($i = 0; $i < 40; $i++) {
	$b->start();
	for ($j = 0; $j < 2000; $j++) {
		$data = igbinary_unserialize($ser);
	}
	$b->stop($j);
	$b->write();
}
