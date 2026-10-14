--TEST--
igbinary_unserialize() only creates Serializable classes through Serializable::unserialize() or __unserialize()
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip requires __unserialize\n"; } ?>
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
class Plain {
    public $x = 1;
}
class Ser1 implements Serializable {
    public $x;
    public function serialize() { return 'x'; }
    public function unserialize($data) { $this->x = $data; }
}
class Ser2 implements Serializable {
    public $x;
    public function serialize() { return 'x'; }
    public function unserialize($data) { $this->x = $data; }
    public function __serialize(): array { return ['x' => 'y']; }
    public function __unserialize(array $data): void { $this->x = $data['x']; }
}
$s = igbinary_serialize(new Plain());
var_dump(igbinary_unserialize(str_replace('Plain', 'Ser1', str_replace("\x05Plain", "\x04Plain", $s))));
var_dump(igbinary_unserialize(str_replace('Plain', 'Ser2', str_replace("\x05Plain", "\x04Plain", $s))));
?>
--EXPECTF--
Warning: Erroneous data format for unserializing 'Ser1' in %s on line %d
NULL
object(Ser2)#%d (1) {
  ["x"]=>
  int(1)
}
