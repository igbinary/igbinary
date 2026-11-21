--TEST--
igbinary_unserialize() must release the previous value when serialized data repeats an array key
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
--FILE--
<?php
// array8 n=2: "a" => "xx", then string_id8(0) ("a" again) => long8p(5)
// The first value is refcounted and gets overwritten by the second; it must be released exactly once.
$data = "\x00\x00\x00\x02\x14\x02" . "\x11\x01a" . "\x11\x02xx" . "\x0e\x00" . "\x06\x05";
var_dump(igbinary_unserialize($data));

// Same with an array as the first value: "a" => [0 => 1], then "a" => 5
$data = "\x00\x00\x00\x02\x14\x02" . "\x11\x01a" . "\x14\x01\x06\x00\x06\x01" . "\x0e\x00" . "\x06\x05";
var_dump(igbinary_unserialize($data));

// Same with an object as the first value: "a" => stdClass{}, then "a" => 5
$data = "\x00\x00\x00\x02\x14\x02" . "\x11\x01a" . "\x17\x08stdClass\x14\x00" . "\x0e\x00" . "\x06\x05";
var_dump(igbinary_unserialize($data));

// Integer keys: 0 => "yy", 0 => 7
$data = "\x00\x00\x00\x02\x14\x02" . "\x06\x00" . "\x11\x02yy" . "\x06\x00" . "\x06\x07";
var_dump(igbinary_unserialize($data));
echo "Done\n";
--EXPECT--
array(1) {
  ["a"]=>
  int(5)
}
array(1) {
  ["a"]=>
  int(5)
}
array(1) {
  ["a"]=>
  int(5)
}
array(1) {
  [0]=>
  int(7)
}
Done
