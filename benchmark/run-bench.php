<?php

$dir = new DirectoryIterator('./');
$stderr = fopen('php://stderr', 'w');

$benchBaseName = getenv('BENCH_BASE_NAME');
if ($benchBaseName === false) {
	$benchBaseName = 'baseline';
}

foreach ($dir as $fileInfo) {
	$fn = $fileInfo->getBasename();
	$name = $fileInfo->getBasename('.b.php');

	if (!preg_match('/\.b\.php$/', $fn)) {
		continue;
	}

	$file = $fileInfo->openFile();
	$file->setFlags(SplFileObject::DROP_NEW_LINE | SplFileObject::SKIP_EMPTY);

	$desc = $fn;
	foreach ($file as $line) {
		if (preg_match('/Description:\s*(.*)/', $line, $m)) {
			$desc = trim($m[1]);
		}
	}

	fwrite($stderr, str_pad($desc, 50, '.'));
	$start = microtime(true);

	$php = getenv('BENCH_PHP_EXECUTABLE');
	if ($php === false) {
		$php = 'php';
	}

	$args = getenv('BENCH_PHP_ARGS');
	if ($args === false) {
		$args = '';
	}

	$output = shell_exec($php . ' ' . $args . ' ' . $fileInfo->getFilename());
	
	$stop = microtime(true);
	fprintf($stderr, "DONE %.2fs\n", $stop - $start);

	file_put_contents($benchBaseName . '-' . $name . '.txt', $output);
}
