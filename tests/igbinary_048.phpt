--TEST--
Object test, __set not called for private attr in extended class
--SKIPIF--
<?php
if(!extension_loaded('igbinary')) {
        echo "skip no igbinary";
}
--FILE--
<?php

class Bar {
    public $a = array();
    public $b = array();
    public $c = NULL;
    private $_d = NULL;
    public function __set($name,$value) {
        echo 'magic function called for ' . $name . ' with ' . var_export($value, true) . PHP_EOL;
    }
}

class Foo extends Bar {
    public $m;
}

$x = new Foo();
$x->a = array(1, 2, 3);
$x->nonexistent = 'aaa';

igbinary_unserialize(igbinary_serialize($x));
--EXPECT--
magic function called for nonexistent with 'aaa'
