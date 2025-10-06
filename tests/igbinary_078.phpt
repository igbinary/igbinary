--TEST--
igbinary and large arrays
--SKIPIF--
<?php
if (PHP_VERSION_ID >= 80500) {
    echo "skip: __sleep() and __wakeup() are deprecated in PHP 8.5+ (see https://wiki.php.net/rfc/deprecations_php_8_5#deprecate_the_sleep_and_wakeup_magic_methods)";
}
?>
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