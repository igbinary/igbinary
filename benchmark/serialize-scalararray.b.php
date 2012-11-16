<?php

// Description: Serialize scalar array

require_once 'bench.php';

$b = new Bench('serialize-scalar-array');

$array = array();
for ($i = 0; $i < 1000; $i++) {
	switch ($i % 4) {
	case 0:
		$array[] = "da string " . $i;
		break;
	case 1:
		$array[] = 1.31 * $i;
		break;
	case 2:
		$array[] = rand(0, PHP_INT_MAX);
		break;
	case 3:
		$array[] = (bool)($i & 1);
		break;
	}
}

for ($i = 0; $i < 40; $i++) {
	$b->start();
	for ($j = 0; $j < 12000; $j++) {
		$ser = igbinary_serialize($array);
	}
	$b->stop($j);
	$b->write();
}
