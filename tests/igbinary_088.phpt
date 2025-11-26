--TEST--
Test serializing wrong values in __sleep
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
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
Notice: igbinary_serialize(): "name0" returned as member variable from __sleep() but does not exist in %s on line 12

Notice: igbinary_serialize(): "name1" returned as member variable from __sleep() but does not exist in %s on line 12

Notice: igbinary_serialize(): "name2" returned as member variable from __sleep() but does not exist in %s on line 12
\x00\x00\x00\x02\x17\x01X\x14\x03\x11\x05name0\x00\x11\x05name1\x00\x11\x05name2\x00
object(X)#2 (3) {
  ["name0"]=>
  NULL
  ["name1"]=>
  NULL
  ["name2"]=>
  NULL
}