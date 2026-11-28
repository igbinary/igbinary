--TEST--
igbinary_unserialize() failures don't expose partially unserialized objects
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip requires typed properties and __unserialize\n"; } ?>
--FILE--
<?php
class Unser {
    public function __serialize(): array { return ['x' => 1]; }
    public function __unserialize(array $data): void { echo "__unserialize\n"; }
    public function __destruct() { echo "Unser::__destruct\n"; }
}
class Wakeup {
    public $x = 1;
    public function __wakeup() { echo "__wakeup\n"; }
    public function __destruct() { echo "Wakeup::__destruct\n"; }
}
class Typed {
    public array $a;
}
class Untyped {
    public $a;
}
class Holder {
    public $c;
    public function __destruct() { var_dump($this->c); }
}
class Holdex {
    public $c;
}

echo "Truncated __unserialize() payload\n";
var_dump(igbinary_unserialize("\x00\x00\x00\x02\x17\x05Unser\x15\xff\xff"));

echo "Invalid data after objects with pending __unserialize() and __wakeup()\n";
$s = igbinary_serialize([new Unser(), new Wakeup(), 1]);
var_dump(igbinary_unserialize(substr($s, 0, -4) . "\xff\x02\x06\x01"));

echo "Invalid data in the value of a typed property\n";
$h = new Holdex();
$h->c = new Untyped();
$h->c->a = new stdClass();
$h->c->a->x = 1;
$h->c->a->y = 2;
$s = str_replace(['Holdex', "\x07Untyped"], ['Holder', "\x05Typed"], igbinary_serialize($h));
var_dump(igbinary_unserialize(str_replace("\x11\x01y", "\xff\x01y", $s)));
echo "Done\n";
?>
--EXPECTF--
Truncated __unserialize() payload

Warning: igbinary_unserialize_array: data size 0 smaller that requested array length 65535. in %s on line %d
NULL
Invalid data after objects with pending __unserialize() and __wakeup()
Unser::__destruct
Wakeup::__destruct

Warning: igbinary_unserialize_array: unknown key type 'ff', position %d in %s on line %d
NULL
Invalid data in the value of a typed property

Warning: igbinary_unserialize_object_properties: unknown key type 'ff', position %d in %s on line %d
object(Typed)#%d (0) {
  ["a"]=>
  uninitialized(array)
}
NULL
Done
