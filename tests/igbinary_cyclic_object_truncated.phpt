--TEST--
igbinary_unserialize() must not leak a self-referencing object when the data is truncated before a property key
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
--FILE--
<?php
class S {
    public $self;
    public $other;
    public function __destruct() { echo "destruct called\n"; }
}
// object8 "S", array8 n=2: "self" => objref8(0) (the object itself), then end-of-data where the 2nd key is expected.
// The object has refcount 2 (return value + self reference) when igbinary gives up.
$data = "\x00\x00\x00\x02\x17\x01S\x14\x02" . "\x11\x04self" . "\x22\x00";
var_dump(igbinary_unserialize($data));
gc_collect_cycles();
echo "Done\n";
--EXPECTF--
Warning: igbinary_unserialize_object_properties: end-of-data in %s on line %d
NULL
destruct called
Done
