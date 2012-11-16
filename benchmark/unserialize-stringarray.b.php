<?php

// Description: Unserialize large strings array

require_once 'bench.php';

$b = new Bench('serialize-string-array');

$ser = igbinary_serialize(unserialize(file_get_contents(dirname(__FILE__) . DIRECTORY_SEPARATOR . 'l10n-en.ser')));

for ($i = 0; $i < 40; $i++) {
	$b->start();
	for ($j = 0; $j < 500; $j++) {
		$array = igbinary_unserialize($ser);
	}
	$b->stop($j);
	$b->write();
}
