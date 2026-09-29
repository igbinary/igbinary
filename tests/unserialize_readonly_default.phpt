--TEST--
__unserialize() can overwrite the default value of readonly properties
--SKIPIF--
<?php if (PHP_VERSION_ID < 80600) { echo "skip requires php 8.6+\n"; } ?>
--FILE--
<?php
class TestDefault {
    public readonly int $prop = 1;
}
var_dump(igbinary_unserialize(igbinary_serialize(new TestDefault())));

class TestDefaultWithUnserialize {
    public readonly int $prop = 1;
    public public(set) readonly int $lock = 1;

    public function __serialize(): array {
        return ['prop' => 2];
    }

    public function __unserialize(array $data): void {
        foreach ($data as $key => $value) {
            $this->{$key} = $value;
        }
    }
}

$test = igbinary_unserialize(igbinary_serialize(new TestDefaultWithUnserialize()));
var_dump($test);

try {
    $test->prop = 3;
} catch (Error $e) {
    echo $e::class, ': ', $e->getMessage(), PHP_EOL;
}

try {
    $test->lock = 3;
} catch (Error $e) {
    echo $e::class, ': ', $e->getMessage(), PHP_EOL;
}
?>
--EXPECT--
object(TestDefault)#1 (1) {
  ["prop"]=>
  int(1)
}
object(TestDefaultWithUnserialize)#1 (2) {
  ["prop"]=>
  int(2)
  ["lock"]=>
  int(1)
}
Error: Cannot modify readonly property TestDefaultWithUnserialize::$prop
Error: Cannot modify readonly property TestDefaultWithUnserialize::$lock
