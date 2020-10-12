<?php

// Description: Unserialize very large array of arrays

require_once 'bench.php';

$b = new Bench('unserialize-largearrayarray');

srand(13333);
$data = [];
for ($i = 0; $i < 1000; $i++) {
    $part = [];
    for ($j = 0, $n = rand() % 100; $j < $n; $j++) {
        $part[] = rand() % 300;
    }
    $data[] = $part;
}
$ser = igbinary_serialize($data);

for ($i = 0; $i < 40; $i++) {
	$b->start();
	for ($j = 0; $j < 500; $j++) {
		$array = igbinary_unserialize($ser);
	}
	$b->stop($j);
	$b->write();
    unset($array);
}
