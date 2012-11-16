<?php

// Description: Serialize large strings array

require_once 'bench.php';

$b = new Bench('serialize-string-array');

$array = unserialize(file_get_contents(dirname(__FILE__) . DIRECTORY_SEPARATOR . 'l10n-en.ser'));

for ($i = 0; $i < 40; $i++) {
	$b->start();
	for ($j = 0; $j < 360; $j++) {
		$ser = igbinary_serialize($array);
	}
	$b->stop($j);
	$b->write();
}
