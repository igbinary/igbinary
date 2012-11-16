--TEST--
Unserialize backwards compatible with v1.
--SKIPIF--
--FILE--
<?php
$data = array(
	array(
		'var' => 'b:1;',
		'type' => 'boolean',
		'description' => 'bool true',
		'data' => 'AAAAAQU=',
		'version' => 1,
	),
	array(
		'var' => 'b:0;',
		'type' => 'boolean',
		'description' => 'bool false',
		'data' => 'AAAAAQQ=',
		'version' => 1,
	),
	array(
		'var' => 'd:1.2881887378882661554513333612703718245029449462890625;',
		'type' => 'double',
		'description' => 'double',
		'data' => 'AAAAAQw/9Jxry0Tj0Q==',
		'version' => 1,
	),
	array(
		'var' => 'i:29913;',
		'type' => 'integer',
		'description' => 'int',
		'data' => 'AAAAAQh02Q==',
		'version' => 1,
	),
	array(
		'var' => 'N;',
		'type' => 'NULL',
		'description' => 'null',
		'data' => 'AAAAAQA=',
		'version' => 1,
	),
	array(
		'var' => 's:0:"";',
		'type' => 'string',
		'description' => 'string',
		'data' => 'AAAAAQ0=',
		'version' => 1,
	),
	array(
		'var' => 's:13:"asdf' . "\0" . 'asdfasdf";',
		'type' => 'string',
		'description' => 'string',
		'data' => 'AAAAARENYXNkZgBhc2RmYXNkZg==',
		'version' => 1,
	),
	array(
		'var' => 'a:4:{i:0;i:1;i:1;i:2;i:2;i:3;i:3;i:4;}',
		'type' => 'array',
		'description' => 'array',
		'data' => 'AAAAARQEBgAGAQYBBgIGAgYDBgMGBA==',
		'version' => 1,
	),
	array(
		'var' => 'O:8:"stdClass":4:{i:0;i:1;i:1;i:2;i:2;i:3;i:3;i:4;}',
		'var_e' => (object)array(1, 2, 3, 4),
		'type' => 'object',
		'description' => 'object',
		'data' => 'AAAAARcIc3RkQ2xhc3MUBAYABgEGAQYCBgIGAwYDBgQ=',
		'version' => 1,
	),
	array(
		'var' => 'a:2:{i:0;a:3:{i:0;s:1:"a";i:1;s:1:"b";i:2;s:1:"c";}i:1;R:2;}',
		'type' => 'array',
		'description' => 'reference',
		'data' => 'AAAAARQCBgAUAwYAEQFhBgERAWIGAhEBYwYBAQE=',
		'version' => 1,
	),
);

$all_passed = true;
foreach ($data as $item) {
	if (isset($item['var_e'])) {
		$var = $item['var_e'];
	} else {
		$var = unserialize($item['var']);
	}
	$unserialized = igbinary_unserialize(base64_decode($item['data']));

	ob_start();
	var_dump($var);
	$dump_expected = ob_get_clean();

	ob_start();
	var_dump($unserialized);
	$dump_actual = ob_get_clean();

	// replace all object ids to 0
	$dump_expected = preg_replace('/#\d+/', '#0', $dump_expected);
	$dump_actual = preg_replace('/#\d+/', '#0', $dump_actual);

	if ($dump_expected !== $dump_actual) {
		if ($item['description'] == 'reference') {
			echo "reference deserialization works, but the result is not a reference.\n";
			continue;
		}

		echo "Differing unserialized: {$item['description']}\n";
		echo "Expected:\n", $dump_expected, "\n";
		echo "Actual:\n", $dump_actual, "\n";
	}
}

echo $all_passed ? 'OK' : 'ERROR', "\n";

--EXPECT--
reference deserialization works, but the result is not a reference.
OK
