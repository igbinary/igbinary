--TEST--
Igbinary should behave like the PHP serializer when the __serialize method of a derived class sets a value to a private property of a base class
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip __serialize/__unserialize not supported in php < 7.4 for compatibility with serialize()"; } ?>
--FILE--
<?php
class BaseClass {
    private int $private_property = 1;
}

class DerivedClass extends BaseClass {

    public function __serialize() {
        return [
            'private_property' => 2,
        ];
    }
}

$original = new DerivedClass();
var_dump($original);

// Serialize / Unserialize with PHP and Igbinary. The behavior must be the same.
echo "\nPHP behavior:\n";
$serialized_php = serialize($original);
$unserialized_php = unserialize($serialized_php);
var_dump($unserialized_php);

echo "\nIgbinary behavior:\n";
$serialized_igb = igbinary_serialize($original);
$unserialized_igb = igbinary_unserialize($serialized_igb);
var_dump($unserialized_igb);

?>
--EXPECT--
object(DerivedClass)#1 (1) {
  ["private_property":"BaseClass":private]=>
  int(1)
}

PHP behavior:
object(DerivedClass)#2 (1) {
  ["private_property":"BaseClass":private]=>
  int(2)
}

Igbinary behavior:
object(DerivedClass)#3 (1) {
  ["private_property":"BaseClass":private]=>
  int(2)
}