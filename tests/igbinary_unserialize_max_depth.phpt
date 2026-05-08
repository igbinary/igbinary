--TEST--
igbinary_unserialize honors unserialize_max_depth ini setting
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip unserialize_max_depth ini was added in PHP 7.4"; } ?>
--FILE--
<?php
// Build a 200-deep nested array. All cap exercises stay shallow on purpose
// so the test does not stress the C stack under valgrind in CI.
$deep = null;
for ($i = 0; $i < 200; $i++) {
    $deep = ['x' => $deep];
}
$ok = igbinary_serialize($deep);

// Cap below payload depth: bail with warning, return null.
ini_set('unserialize_max_depth', '100');
var_dump(igbinary_unserialize($ok));

// Cap above payload depth: round-trips successfully.
ini_set('unserialize_max_depth', '500');
var_dump(igbinary_unserialize($ok) !== null);

// Cap = 0 disables the limit, matching PHP core unserialize() semantics.
ini_set('unserialize_max_depth', '0');
var_dump(igbinary_unserialize($ok) !== null);

// Synthetic deeply-nested payload: depth-bomb is rejected at the small cap
// without ever recursing past 100 frames, proving the crash protection.
ini_set('unserialize_max_depth', '100');
$bomb = "\x00\x00\x00\x02" . str_repeat("\x14\x01\x11\x01x", 5000) . "\x00";
var_dump(igbinary_unserialize($bomb));
?>
--EXPECTF--
Warning: igbinary_unserialize(): Maximum depth of 100 exceeded. The depth limit can be changed using the unserialize_max_depth ini setting in %s on line %d
NULL
bool(true)
bool(true)

Warning: igbinary_unserialize(): Maximum depth of 100 exceeded. The depth limit can be changed using the unserialize_max_depth ini setting in %s on line %d
NULL
