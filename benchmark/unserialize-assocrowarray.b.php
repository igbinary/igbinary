<?php

// Description: Unserialize array of associative arrays (e.g. database rows)

require_once 'bench.php';

call_user_func(function () {
	$b = new Bench('unserialize-assoc-row-array');

	$array = [];
	for ($i = 0; $i < 200; $i++) {
		$array[] = [
			'id' => $i,
			'name' => "user_$i",
			'email' => "user$i@example.com",
			'score' => $i * 1.5,
			'active' => ($i % 2) === 0,
			'created_at' => '2026-10-23 10:00:00',
			'tags' => ['a', 'b', 'c'],
			'count' => $i * 1000,
			'ratio' => 0.25,
			'nullable' => null,
		];
	}
	$ser = igbinary_serialize($array);

	for ($i = 0; $i < 40; $i++) {
		$b->start();
		for ($j = 0; $j < 3000; $j++) {
			$array = igbinary_unserialize($ser);
		}
		$b->stop($j);
		$b->write();
	}
});
