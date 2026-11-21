--TEST--
igbinary_unserialize() must not leak a self-referencing array when the data is truncated before an array key (issue #425)
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
--FILE--
<?php
// array8 n=5: key long8p(20) => ref16(0) (the array itself), then end-of-data where the next key is expected.
// The partially built array has refcount 2 (return value + self reference) when igbinary gives up.
// Releasing it without registering a GC root leaks the cycle for the rest of the request.
$data = "\x00\x00\x00\x02\x14\x05\x06\x14\x02\x00\x00";
var_dump(igbinary_unserialize($data));
var_dump(gc_collect_cycles());
echo "Done\n";
--EXPECTF--
Warning: igbinary_unserialize_array: end-of-data in %s on line %d
NULL
int(%d)
Done
