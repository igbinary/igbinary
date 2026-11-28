--TEST--
Objects without a properties table are serialized without building one
--SKIPIF--
<?php if (PHP_VERSION_ID < 80100) { echo "skip requires php 8.1+\n"; } ?>
--FILE--
<?php
class P {
    private $p = 'parent';
    protected $q;
}
class C extends P {
    public int $a;
    public ?string $b = null;
    private $p = 'child';
    public $unset = 1;
}
$c = new C();
unset($c->unset);
$s = igbinary_serialize($c);
echo bin2hex($s), "\n";
var_dump(igbinary_unserialize($s));

// Uninitialized properties are skipped, like serialize() does
get_object_vars($c);
var_dump(igbinary_unserialize(igbinary_serialize($c)) == igbinary_unserialize($s));

$objects = [];
for ($i = 0; $i < 1000; $i++) {
    $objects[] = new C();
}
$before = memory_get_usage();
$s = igbinary_serialize($objects);
unset($s);
var_dump(memory_get_usage() - $before < 50000);
?>
--EXPECT--
0000000217014314041104005000701106706172656e741104002a0071001101620011040043007011056368696c64
object(C)#2 (5) {
  ["p":"P":private]=>
  string(6) "parent"
  ["q":protected]=>
  NULL
  ["a"]=>
  uninitialized(int)
  ["b"]=>
  NULL
  ["p":"C":private]=>
  string(5) "child"
  ["unset"]=>
  int(1)
}
bool(true)
bool(true)