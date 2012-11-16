<?php

// Description: Serialize scalar int

require_once 'bench.php';

$b = new Bench('serialize-scalar-int');

$var = 1;

for ($i = 0; $i < 40; $i++) {
	$b->start();
	for ($j = 0; $j < 3500000; $j++) {
		$ser = igbinary_serialize($var);
	}
	$b->stop($j);
	$b->write();
}
