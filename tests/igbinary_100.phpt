--TEST--
Check if igbinary behaves like the serializer of PHP >= 7.2 when the visibility levels of properties are changed between serialization and unserialization
--SKIPIF--
<?php if (PHP_VERSION_ID < 70200) { echo "skip php < 7.2"; } ?>
--FILE--
<?php

// The following class + code was used to generate the serialized data (with PHP 8.5):
// class Test {
//     private int $private_property = 1;
//     protected int $protected_property = 2;
//     public int $public_property = 3;
// }
// $serialized_php = bin2hex(serialize(new Test()));
// $serialized_igb = bin2hex(igbinary_serialize(new Test()));
//
$serialized_php = '4f3a343a2254657374223a333a7b733a32323a22005465737400707269766174655f70726f7065727479223b693a313b733a32313a22002a0070726f7465637465645f70726f7065727479223b693a323b733a31353a227075626c69635f70726f7065727479223b693a333b7d';
$serialized_igb = '0000000217045465737414031116005465737400707269766174655f70726f706572747906011115002a0070726f7465637465645f70726f70657274790602110f7075626c69635f70726f70657274790603';

// Define the same class, but with different visibility levels
class Test {
    public $private_property = 1;    // Visibility was changed from private to public
    private $protected_property = 2; // Visibility was changed from protected to private
    protected $public_property = 3;  // Visibility was changed from public to protected
}

// Serialize / Unserialize with PHP and Igbinary. The behavior must be the same.
echo "PHP behavior:\n";
$unserialized_php = unserialize(hex2bin($serialized_php));
var_dump($unserialized_php);

echo "\nIgbinary behavior:\n";
$unserialized_igb = igbinary_unserialize(hex2bin($serialized_igb));
var_dump($unserialized_igb);

?>
--EXPECT--
PHP behavior:
object(Test)#1 (3) {
  ["private_property"]=>
  int(1)
  ["protected_property":"Test":private]=>
  int(2)
  ["public_property":protected]=>
  int(3)
}

Igbinary behavior:
object(Test)#2 (3) {
  ["private_property"]=>
  int(1)
  ["protected_property":"Test":private]=>
  int(2)
  ["public_property":protected]=>
  int(3)
}
