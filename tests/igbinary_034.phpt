--TEST--
Unserialize invalid random data
--SKIPIF--
<?php
if(!extension_loaded('igbinary')) {
	echo "skip no igbinary";
}
--FILE--
<?php

function rndbytes() {
	if (function_exists('random_bytes')) {
		return random_bytes(40);
	}

	if (function_exists('mcrypt_create_iv')) {
		return mcrypt_create_iv(30, MCRYPT_DEV_URANDOM);
	}

	$rnd = [];
	for ($i = 0; $i < 40; $i++) {
		$rnd[] = chr(rand(0, 255));
	}
	return implode('', $rnd);
}

$datas = array(
	87817,
	-1,
	array(1,2,3,"testing" => 10, "foo"),
	true,
	false,
	0.187182,
	"dakjdh98389\000",
	null,
	(object)array(1,2,3),
);

error_reporting(~(E_WARNING|E_NOTICE));
foreach ($datas as $data) {
	$str = igbinary_serialize($data);
	$len = strlen($str);

	for ($j = 0; $j < 200; $j++) {
		for ($i = 0; $i < $len - 1; $i++) {
			$sub = substr($str, 0, $i);
			$sub .= rndbytes();
			$php_errormsg = null;
			$v = igbinary_unserialize($sub);
		}
	}
}
echo "Done\n";
--EXPECT--
Done
