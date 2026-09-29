--TEST--
igbinary_unserialize counts the depth of nested arrays and objects like unserialize()
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip unserialize_max_depth ini was added in PHP 7.4"; } ?>
--FILE--
<?php
function test($label, $value) {
    $php = unserialize(serialize($value));
    $igbinary = @igbinary_unserialize(igbinary_serialize($value));
    echo $label, ': ', $php === false ? 'false' : 'ok', ' ', $igbinary === null ? 'null' : 'ok', "\n";
}
ini_set('unserialize_max_depth', 1);
test('1', 1);
test('[1]', [1]);
test('[[]]', [[]]);
test('[[1]]', [[1]]);
test('{}', new stdClass());
test('[{}]', [new stdClass()]);
ini_set('unserialize_max_depth', 2);
test('[[1]]', [[1]]);
test('[[[1]]]', [[[1]]]);
test('[ArrayObject([1])]', ['a' => new ArrayObject([1])]);

// References to references are never emitted by igbinary_serialize()
var_dump(igbinary_unserialize("\x00\x00\x00\x02\x25\x25\x06\x01"));
?>
--EXPECTF--
1: ok ok
[1]: ok ok
[[]]: ok ok

Warning: unserialize(): Maximum depth of 1 exceeded. %s

%s: unserialize(): Error at offset %d of %d bytes in %s on line %d
[[1]]: false null
{}: ok ok

Warning: unserialize(): Maximum depth of 1 exceeded. %s

%s: unserialize(): Error at offset %d of %d bytes in %s on line %d
[{}]: false null
[[1]]: ok ok

Warning: unserialize(): Maximum depth of 2 exceeded. %s

%s: unserialize(): Error at offset %d of %d bytes in %s on line %d
[[[1]]]: false null

Warning: unserialize(): Maximum depth of 2 exceeded. %s

%s: unserialize(): Error at offset %d of %d bytes in %s on line %d
[ArrayObject([1])]: false null

Warning: igbinary_unserialize_zval: unexpected reference to a reference, position 5 in %s on line %d
NULL