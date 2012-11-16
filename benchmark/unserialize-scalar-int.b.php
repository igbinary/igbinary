<?php

// Description: Unserialize scalar int

require_once 'bench.php';

$b = new Bench('serialize-scalar-int');

$ser = igbinary_serialize(1);

for ($i = 0; $i < 40; $i++) {
	$b->start();
	for ($j = 0; $j < 3600000; $j++) {
		$var = igbinary_unserialize($ser);
	}
	$b->stop($j);
	$b->write();
}
