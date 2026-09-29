--TEST--
igbinary_unserialize() supports the allowed_classes and max_depth options of unserialize()
--SKIPIF--
<?php if (PHP_VERSION_ID < 80100) { echo "skip requires php 8.1+\n"; } ?>
--FILE--
<?php
enum Suit: string {
    case Hearts = 'H';
}
class Foo {
    public $x = 1;
}
class Bar {
    public $foo;
    public function __construct() {
        $this->foo = new Foo();
    }
}
$s = igbinary_serialize([new Bar(), Suit::Hearts]);

function test(array $options) {
    global $s;
    echo json_encode($options), "\n";
    try {
        $value = igbinary_unserialize($s, $options);
        if ($value === null) {
            echo "null\n";
        } else {
            echo get_class($value[0]), ' ', get_class(((array) $value[0])['foo']), ' ', $value[1]->name, "\n";
        }
    } catch (Throwable $e) {
        echo get_class($e), ': ', $e->getMessage(), "\n";
    }
}
test([]);
test(['allowed_classes' => true]);
test(['allowed_classes' => false]);
test(['allowed_classes' => []]);
test(['allowed_classes' => ['foo']]);
test(['allowed_classes' => ['BAR', 'Foo']]);
test(['max_depth' => 2]);
test(['max_depth' => 3]);
test(['max_depth' => 0]);
test(['allowed_classes' => 'Foo']);
test(['allowed_classes' => [1]]);
test(['allowed_classes' => ['Foo Bar']]);
test(['max_depth' => '1']);
test(['max_depth' => -1]);
?>
--EXPECTF--
[]
Bar Foo Hearts
{"allowed_classes":true}
Bar Foo Hearts
{"allowed_classes":false}
__PHP_Incomplete_Class __PHP_Incomplete_Class Hearts
{"allowed_classes":[]}
__PHP_Incomplete_Class __PHP_Incomplete_Class Hearts
{"allowed_classes":["foo"]}
__PHP_Incomplete_Class Foo Hearts
{"allowed_classes":["BAR","Foo"]}
Bar Foo Hearts
{"max_depth":2}

Warning: igbinary_unserialize(): Maximum depth of 2 exceeded. The depth limit can be changed using the max_depth igbinary_unserialize() option or the unserialize_max_depth ini setting in %s on line %d
null
{"max_depth":3}
Bar Foo Hearts
{"max_depth":0}
Bar Foo Hearts
{"allowed_classes":"Foo"}
TypeError: igbinary_unserialize(): Option "allowed_classes" must be of type array|bool, string given
{"allowed_classes":[1]}
TypeError: igbinary_unserialize(): Option "allowed_classes" must be an array of class names, int given
{"allowed_classes":["Foo Bar"]}
ValueError: igbinary_unserialize(): Option "allowed_classes" must be an array of class names, "Foo Bar" given
{"max_depth":"1"}
TypeError: igbinary_unserialize(): Option "max_depth" must be of type int, string given
{"max_depth":-1}
ValueError: igbinary_unserialize(): Option "max_depth" must be greater than or equal to 0
