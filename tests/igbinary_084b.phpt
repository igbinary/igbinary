--TEST--
Properly free duplicate undeclared properties when unserializing invalid data
--SKIPIF--
<?php
if (PHP_VERSION_ID >= 90000) { echo "skip requires php < 9.0 when testing that the deprecation has no impact on igbinary functionality\n"; }
?>
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
if (PHP_VERSION_ID >= 80200) { require_once __DIR__ . '/php82_suppress_dynamic_properties_warning.inc'; }
class Test {
    public function __construct( ) {
        $this->pub = null;
    }

    public function __sleep() {
        // TODO: Could start detecting duplicates and emitting a notice as well
        return ["pub", "pub"];
    }
}
$t = new Test();
$t->pub = new Test();
$s = igbinary_serialize($t);
echo urlencode($s), "\n";
$unser = igbinary_unserialize($s);
var_dump($unser);
?>
--EXPECT--
%00%00%00%02%17%04Test%14%02%11%03pub%1A%00%14%02%0E%01%00%0E%01%00%0E%01%22%01
object(Test)#3 (1) {
  ["pub"]=>
  object(Test)#4 (1) {
    ["pub"]=>
    NULL
  }
}
