--TEST--
Test serializing wrong values in __sleep
--SKIPIF--
<?php if (PHP_VERSION_ID < 80100) { echo "skip requires php 8.1+\n"; } ?>
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
#[AllowDynamicProperties]
class X {
    public function __sleep() {
        return $this;
    }
}
$x = new X();
for ($i = 0; $i < 3; $i++) {
    $x->{"p$i"} = "name$i";
}
$ser = igbinary_serialize($x);
$unser = igbinary_unserialize($ser);
echo str_replace(['\\', '%'], ['\\\\', '\x'], urlencode($ser)), "\n";
var_dump($unser);

?>
--EXPECTF--
Warning: igbinary_serialize(): X::__sleep() should return an array only containing the names of instance-variables to serialize in %s on line 12
\x00\x00\x00\x02\x00
NULL
