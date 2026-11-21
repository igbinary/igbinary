--TEST--
igbinary_unserialize does not leak a partially built array when an element value fails
--FILE--
<?php
// Each payload is an array8 with n=2 whose first element is fine and whose second element
// fails while its *value* (not its key) is being unserialized. igbinary_unserialize_array
// returns without releasing the outer array it has already allocated; the PHP function wrapper
// catches that with zval_ptr_dtor(return_value), the session decoder does not (see
// igbinary_claude_session_decode_failure.phpt). This test guards the wrapper's catch-all.

// "k" => [0 => "hello", 1 => "world", <missing 3rd element>]: nested array truncated before its 3rd key.
var_dump(igbinary_unserialize("\x00\x00\x00\x02\x14\x02" . "\x11\x01k" . "\x14\x03\x06\x00\x11\x05hello\x06\x01\x11\x05world"));

// "k" => "hello", "j" => <end-of-data where the value type byte should be>.
var_dump(igbinary_unserialize("\x00\x00\x00\x02\x14\x02" . "\x11\x01k" . "\x11\x05hello" . "\x11\x01j"));

// "k" => "hello", "j" => string8 that claims 5 bytes but only 2 remain.
var_dump(igbinary_unserialize("\x00\x00\x00\x02\x14\x02" . "\x11\x01k" . "\x11\x05hello" . "\x11\x01j" . "\x11\x05ab"));

// Same shape through an object: stdClass with a property whose value is truncated.
var_dump(igbinary_unserialize("\x00\x00\x00\x02\x17\x08stdClass\x14\x02" . "\x11\x01k" . "\x11\x05hello" . "\x11\x01j"));

echo "Done\n";
--EXPECTF--
Warning: igbinary_unserialize_array: end-of-data in %s on line %d
NULL

Warning: igbinary_unserialize_zval: end-of-data in %s on line %d
NULL

Warning: igbinary_unserialize_chararray: end-of-data in %s on line %d
NULL

Warning: igbinary_unserialize_zval: end-of-data in %s on line %d
NULL
Done
