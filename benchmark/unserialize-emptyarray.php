<?php

// Description: Unserialize array of small integers
// NOTE: Unserialization of positive integers in the range 0-65536 is specifically optimized.
// (they're common as counts, offsets, array indexes, etc.)

require_once 'bench.php';

$b = new Bench('unserialize-intarray');

$ser = igbinary_serialize([]);

for ($i = 0; $i < 40; $i++) {
	$b->start();
	$results = [];
	for ($j = 0; $j < 500000; $j++) {
		$results[] = igbinary_unserialize($ser);
	}
	$b->stop($j);
	$b->write();
}
