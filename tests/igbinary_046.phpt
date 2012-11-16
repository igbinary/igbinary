--TEST--
Correctly unserialize scalar refs.
--SKIPIF--
--INI--
igbinary.compact_strings = On
--FILE--
<?php 
$a = array("A");
$a[1] = &$a[0];
$a[2] = &$a[1];
$a[3] = &$a[2];

$ig_ser = igbinary_serialize($a);
$ig = igbinary_unserialize($ig_ser);
$f = &$ig[3];
$f = 'V';
var_dump($ig);
--EXPECT--
array(4) {
  [0]=>
  &string(1) "V"
  [1]=>
  &string(1) "V"
  [2]=>
  &string(1) "V"
  [3]=>
  &string(1) "V"
}
