--TEST--
Test serializing wrong values in __sleep (php 8.0)
--SKIPIF--
<?php if (PHP_VERSION_ID < 80000 || PHP_VERSION_ID >= 80100) { echo "skip requires php 8.0\n"; } ?>
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
Warning: igbinary_serialize(): "name0" returned as member variable from __sleep() but does not exist in %s on line 12

Warning: igbinary_serialize(): "name1" returned as member variable from __sleep() but does not exist in %s on line 12

Warning: igbinary_serialize(): "name2" returned as member variable from __sleep() but does not exist in %s on line 12
\x00\x00\x00\x02\x17\x01X\x14\x00
object(X)#2 (0) {
}
