--TEST--
igbinary_unserialize() must not use an uninitialized __unserialize() argument when the property data is truncated
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
--FILE--
<?php
class U {
    public $x;
    public function __unserialize(array $data): void { echo "Should not call\n"; $this->x = $data; }
    public function __destruct() { }
}
// object8 "U", then the array8 type byte and nothing else: the element count is missing.
$data = "\x00\x00\x00\x02\x17\x01U\x14";
var_dump(igbinary_unserialize($data));

// object8 "U", array8 with n=5 but no element data at all.
$data = "\x00\x00\x00\x02\x17\x01U\x14\x05";
var_dump(igbinary_unserialize($data));

// object8 "U", array8 n=1, key long8p(0) and then end-of-data: the array was allocated but no element was read.
$data = "\x00\x00\x00\x02\x17\x01U\x14\x01\x06\x00";
var_dump(igbinary_unserialize($data));
echo "Done\n";
--EXPECTF--
Warning: igbinary_unserialize_%s: end-of-data in %s on line %d
NULL

Warning: igbinary_unserialize_%s: data size 0 smaller that requested array length 5. in %s on line %d
NULL

Warning: igbinary_unserialize_zval: end-of-data in %s on line %d
NULL
Done
