--TEST--
igbinary_serialize() must release the object properties table when serializing a property fails
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
--FILE--
<?php
class Foo {
    public $a = 1;
    public $b;
}

#[AllowDynamicProperties]
class Dyn {
}

function try_serialize($value) {
    try {
        var_dump(igbinary_serialize($value));
    } catch (Throwable $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
    }
}

// Declared property holding a value that cannot be serialized.
$o = new Foo();
$o->b = function () {};
try_serialize($o);

// Dynamic properties: the properties table owns the values, so leaking the table leaks them too.
$d = new Dyn();
$d->s = str_repeat("x", 64);
$d->c = function () {};
try_serialize($d);

// Nested: the failing object is a property of another object.
$outer = new Foo();
$outer->b = $o;
try_serialize($outer);

unset($o, $d, $outer);
echo "Done\n";
--EXPECT--
Exception: Serialization of 'Closure' is not allowed
Exception: Serialization of 'Closure' is not allowed
Exception: Serialization of 'Closure' is not allowed
Done
