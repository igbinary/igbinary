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
// igbinary currently does not emit a notice or check for duplicates
$s = igbinary_serialize(new Test);
echo urlencode($s), "\n";
var_dump(igbinary_unserialize($s));
?>
--EXPECT--
%00%00%00%02%17%04Test%14%02%11%0A%00Test%00priv%00%0E%01%00
object(Test)#1 (1) {
  ["priv":"Test":private]=>
  NULL
}
