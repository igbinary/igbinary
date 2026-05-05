--TEST--
Name of virtual-property must not be truncated in warning message when visibility was originally protected/private
--SKIPIF--
<?php if (PHP_VERSION_ID < 80400) { echo "skip virtual properties are not supported in php < 8.4"; } ?>
--FILE--
<?php
// The following class + code was used to generate the serialized data (with PHP 8.5):
//   class Test { protected int $prop = 1; }
//   $serialized_php = bin2hex(serialize(new Test()));
//   $serialized_igb = bin2hex(igbinary_serialize(new Test()));
//
$serialized_php = hex2bin('4f3a343a2254657374223a313a7b733a373a22002a0070726f70223b693a313b7d');
$serialized_igb = hex2bin('0000000217045465737414011107002a0070726f700601');

// Same class name, but `prop` is now a *public* virtual property
class Test {
    public int $prop {
        get => 1;
    }
}

echo "PHP behavior:";
unserialize($serialized_php);

echo "\nIgbinary behavior:";
igbinary_unserialize($serialized_igb);

?>
--EXPECTF--
PHP behavior:
Warning: unserialize(): Cannot unserialize value for virtual property Test::$ in %s on line %d

Warning: unserialize(): Error at offset %d of %d bytes in %s on line %d

Igbinary behavior:
Warning: igbinary_unserialize(): Cannot unserialize value for virtual property Test::$prop in %s on line %d
