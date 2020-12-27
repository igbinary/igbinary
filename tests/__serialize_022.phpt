--TEST--
__serialize() mechanism (021): Test __unserialize without __serialize
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip __serialize/__unserialize not supported in php < 7.4 for compatibility with serialize()"; } ?>
--FILE--
<?php

class Test {
    const SOME_CONST = ['key' => 'value'];

    public function __serialize() {
        return self::SOME_CONST;
    }

    public static function test_serialize() {
        $x = igbinary_serialize([self::SOME_CONST, new self(), new self()]);
        // aggressively reuses arrays
        echo urlencode($x), "\n";
        var_dump(igbinary_unserialize($x));
    }
}

Test::test_serialize();
?>
--EXPECT--
%00%00%00%02%14%03%06%00%14%01%11%03key%11%05value%06%01%17%04Test%14%01%0E%00%0E%01%06%02%1A%02%14%01%0E%00%0E%01
array(3) {
  [0]=>
  array(1) {
    ["key"]=>
    string(5) "value"
  }
  [1]=>
  object(Test)#2 (1) {
    ["key"]=>
    string(5) "value"
  }
  [2]=>
  object(Test)#1 (1) {
    ["key"]=>
    string(5) "value"
  }
}
