--TEST--
Test serializing string > 4G
--INI--
memory_limit=15G
--CONFLICTS--
high_memory
--SKIPIF--
<?php
if (!extension_loaded("igbinary")) print "skip\n";
if (PHP_INT_SIZE <= 4) { print "skip requires 74-bit\n"; }
if (!getenv('IGBINARY_HIGH_MEMORY_TESTS')) { print "skip requires IGBINARY_HIGH_MEMORY_TESTS=1\n"; }
?>
--FILE--
<?php
ini_set('memory_limit', '15G');
$ser = igbinary_serialize(str_repeat('*', 4200000000));
echo "len=" . strlen($ser) . "\n";
echo bin2hex(substr($ser, 0, 20)) . "\n";
$unser = igbinary_unserialize($ser);
unset($ser);
var_dump($unser === str_repeat('*', 4200000000));

?>
--EXPECTF--
len=4200000009
0000000213fa56ea002a2a2a2a2a2a2a2a2a2a2a
bool(true)
