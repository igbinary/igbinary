--TEST--
Hydrating virtual properties should fail, just like with the PHP serializer
--SKIPIF--
<?php if (PHP_VERSION_ID < 80400) { echo "skip virtual properties are not supported in php < 8.4"; } ?>
--FILE--
<?php
class Test {

    public int $virtual_property {
        get => 1;
    }

    public function __serialize() {
        return [
            'virtual_property' => 2,
        ];
    }
}

$original = new Test();

// Serialize / Unserialize with PHP and Igbinary. The behavior must be the same.
echo "\nPHP behavior:";
$serialized_php = serialize($original);
$unserialized_php = unserialize($serialized_php);
var_dump($unserialized_php);

echo "\nIgbinary behavior:";
$serialized_igb = igbinary_serialize($original);
$unserialized_igb = igbinary_unserialize($serialized_igb);
var_dump($unserialized_igb);

?>
--EXPECTF--
PHP behavior:
Warning: unserialize(): Cannot unserialize value for virtual property Test::$virtual_property in %s on line %d

Warning: unserialize(): Error at offset 38 of 43 bytes in %s on line %d
bool(false)

Igbinary behavior:
Warning: igbinary_unserialize(): Cannot unserialize value for virtual property Test::$virtual_property in %s on line %d
NULL