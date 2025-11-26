--TEST--
igbinary and large arrays
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
class BadSleep {
    public $prop = 'x';
    public function __construct($value) {
        $this->prop = $value;
    }
    public function __sleep() {
        return null;
    }
}
var_dump(bin2hex($s = igbinary_serialize(new BadSleep('override'))));
var_dump(igbinary_unserialize($s));
?>
--EXPECTF--
Notice: igbinary_serialize(): __sleep should return an array only containing the names of instance-variables to serialize in %s on line %d
string(32) "000000021708426164536c6565701400"
object(BadSleep)#1 (1) {
  ["prop"]=>
  string(1) "x"
}