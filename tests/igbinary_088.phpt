--TEST--
Test serializing wrong values in __sleep
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
--FILE--
<?php
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
echo urlencode($ser), "\n";
var_dump($unser);

?>
--EXPECTF--
Notice: igbinary_serialize(): "name0" returned as member variable from __sleep() but does not exist in %s on line 11

Notice: igbinary_serialize(): "name1" returned as member variable from __sleep() but does not exist in %s on line 11

Notice: igbinary_serialize(): "name2" returned as member variable from __sleep() but does not exist in %s on line 11
%00%00%00%02%17%01X%14%03%11%05name0%00%11%05name1%00%11%05name2%00
object(X)#2 (3) {
  ["name0"]=>
  NULL
  ["name1"]=>
  NULL
  ["name2"]=>
  NULL
}