--TEST--
Don't emit zval has unknown type 0 (IS_UNDEF) (php 7.2 to 7.4)
--SKIPIF--
<?php if (PHP_VERSION_ID < 70200 || PHP_VERSION_ID >= 80000) { echo "skip requires php 7.2 to 7.4, older versions don't map unmangled names to declared properties when unserializing\n"; } ?>
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
function var_export_normalized($value) { echo ltrim(var_export($value, true), "\\"); }
class MyClass {
    public $kept = 2;
    public $x;
    protected $y;
    private $z;
    protected $set = 2;
    private $priv = 2;
    private $omitted = 'myVal';

    public function __sleep() {
        unset($this->x);
        unset($this->y);
        unset($this->z);
        $this->set = 'setVal';
        $this->priv = null;
        $this->omitted = 'otherVal';

        return ['kept', 'x', 'y', 'z', 'set', 'priv'];
    }
}
error_reporting(E_ALL);
$serialized = igbinary_serialize(new MyClass());
echo bin2hex($serialized) . "\n";
var_export_normalized(igbinary_unserialize($serialized));
echo "\n";
?>
--EXPECTF--
Notice: igbinary_serialize(): "x" returned as member variable from __sleep() but does not exist in %s on line 24

Notice: igbinary_serialize(): "y" returned as member variable from __sleep() but does not exist in %s on line 24

Notice: igbinary_serialize(): "z" returned as member variable from __sleep() but does not exist in %s on line 24
0000000217074d79436c617373140611046b6570740602110178001101790011017a001106002a00736574110673657456616c110d004d79436c617373007072697600
MyClass::__set_state(array(
   'kept' => 2,
   'x' => NULL,
   'y' => NULL,
   'z' => NULL,
   'set' => 'setVal',
   'priv' => NULL,
   'omitted' => 'myVal',
))
