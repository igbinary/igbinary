--TEST--
__sleep() returns properties clashing only after mangling
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
class Test {
    private $priv;
    public function __sleep() {
        return ["\0Test\0priv", "priv"];
    }
}
$s = igbinary_serialize(new Test);
echo bin2hex($s), "\n";
var_dump(igbinary_unserialize($s));
?>
--EXPECTF--
%s: igbinary_serialize(): "priv" is returned from __sleep() multiple times in %s on line %d
000000021704546573741401110a0054657374007072697600
object(Test)#1 (1) {
  ["priv":"Test":private]=>
  NULL
}
