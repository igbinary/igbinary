--TEST--
Check hydration of properties with different property visibility levels when __serialize is present but __unserialize is missing
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip __serialize/__unserialize not supported in php < 7.4 for compatibility with serialize()"; } ?>
--FILE--
<?php
class Test {
    private int $private_property = 1;
    protected int $protected_property = 2;
    public int $public_property = 3;

    public function __serialize() {
        return [
            'private_property' => $this->private_property * 10,
            'protected_property' => $this->protected_property * 10,
            'public_property' => $this->public_property * 10,
        ];
    }
}

$original = new Test();
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
object(Test)#1 (3) {
  ["private_property":"Test":private]=>
  int(1)
  ["protected_property":protected]=>
  int(2)
  ["public_property"]=>
  int(3)
}

PHP behavior:
object(Test)#2 (3) {
  ["private_property":"Test":private]=>
  int(10)
  ["protected_property":protected]=>
  int(20)
  ["public_property"]=>
  int(30)
}

Igbinary behavior:
object(Test)#3 (3) {
  ["private_property":"Test":private]=>
  int(10)
  ["protected_property":protected]=>
  int(20)
  ["public_property"]=>
  int(30)
}
