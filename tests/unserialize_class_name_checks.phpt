--TEST--
igbinary_unserialize() rejects class names that unserialize() rejects
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
class Abcd {
    public $x = 1;
}
function mycallback($class) {
    echo "mycallback($class)\n";
}
ini_set('unserialize_callback_func', 'mycallback');
spl_autoload_register(function ($class) {
    echo "autoload($class)\n";
    if ($class === 'Thrw') {
        throw new RuntimeException("Failed loading $class");
    }
});
$s = igbinary_serialize([new Abcd()]);
foreach (['Abcd', 'Miss', "\\Abc", "\0Abc", "A-bc", 'Thrw'] as $name) {
    echo str_replace("\0", '\0', $name), "\n";
    try {
        var_dump(igbinary_unserialize(str_replace('Abcd', $name, $s)));
    } catch (Exception $e) {
        echo get_class($e), ': ', $e->getMessage(), "\n";
    }
}
?>
--EXPECTF--
Abcd
array(1) {
  [0]=>
  object(Abcd)#%d (1) {
    ["x"]=>
    int(1)
  }
}
Miss
autoload(Miss)
mycallback(Miss)
autoload(Miss)

Warning: igbinary_unserialize(): Function mycallback() hasn't defined the class it was called for in %s on line %d
array(1) {
  [0]=>
  object(__PHP_Incomplete_Class)#%d (2) {
    ["__PHP_Incomplete_Class_Name"]=>
    string(4) "Miss"
    ["x"]=>
    int(1)
  }
}
\Abc

Warning: igbinary_unserialize_object: invalid class name, position %d in %s on line %d
NULL
\0Abc

Warning: igbinary_unserialize_object: invalid class name, position %d in %s on line %d
NULL
A-bc

Warning: igbinary_unserialize_object: invalid class name, position %d in %s on line %d
NULL
Thrw
autoload(Thrw)
RuntimeException: Failed loading Thrw
