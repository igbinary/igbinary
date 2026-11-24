--TEST--
igbinary_serialize() __serialize throws
--SKIPIF--
<?php if (!extension_loaded("igbinary") || PHP_VERSION_ID < 70400) print "skip"; ?>
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

// A property whose __serialize() throws.
class Thrower {
    public function __serialize(): array { throw new RuntimeException("nope"); }
    public function __unserialize(array $data): void {}
}
$t = new Foo();
$t->b = new Thrower();
try_serialize($t);

unset($t);
echo "Done\n";
--EXPECT--
RuntimeException: nope
Done
