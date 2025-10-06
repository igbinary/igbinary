--TEST--
__serialize() mechanism (004): Delayed __unserialize() calls
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip __serialize/__unserialize not supported in php < 7.4 for compatibility with serialize()\n"; }
if (PHP_VERSION_ID >= 80500) {
    echo "skip: __sleep() and __wakeup() are deprecated in PHP 8.5+ (see https://wiki.php.net/rfc/deprecations_php_8_5#deprecate_the_sleep_and_wakeup_magic_methods)";
}
?>
--FILE--
<?php

#[AllowDynamicProperties]
class Wakeup {
    public $data;
    public function __construct(array $data) {
        $this->data = $data;
    }
    public function __wakeup() {
        echo "__wakeup() called\n";
        var_dump($this->data);
        $this->woken_up = true;
    }
}

#[AllowDynamicProperties]
class Unserialize {
    public $data;
    public function __construct(array $data) {
        $this->data = $data;
    }
    public function __serialize() {
        return $this->data;
    }
    public function __unserialize(array $data) {
        $this->data = $data;
        echo "__unserialize() called\n";
        var_dump($this->data);
        $this->unserialized = true;
    }
}

$obj = new Wakeup([new Unserialize([new Wakeup([new Unserialize([])])])]);
var_dump(bin2hex($s = igbinary_serialize($obj)));
var_dump(igbinary_unserialize($s));

?>
--EXPECT--
string(110) "00000002170657616b657570140111046461746114010600170b556e73657269616c697a65140106001a0014010e01140106001a021400"
__unserialize() called
array(0) {
}
__wakeup() called
array(1) {
  [0]=>
  object(Unserialize)#8 (2) {
    ["data"]=>
    array(0) {
    }
    ["unserialized"]=>
    bool(true)
  }
}
__unserialize() called
array(1) {
  [0]=>
  object(Wakeup)#7 (2) {
    ["data"]=>
    array(1) {
      [0]=>
      object(Unserialize)#8 (2) {
        ["data"]=>
        array(0) {
        }
        ["unserialized"]=>
        bool(true)
      }
    }
    ["woken_up"]=>
    bool(true)
  }
}
__wakeup() called
array(1) {
  [0]=>
  object(Unserialize)#6 (2) {
    ["data"]=>
    array(1) {
      [0]=>
      object(Wakeup)#7 (2) {
        ["data"]=>
        array(1) {
          [0]=>
          object(Unserialize)#8 (2) {
            ["data"]=>
            array(0) {
            }
            ["unserialized"]=>
            bool(true)
          }
        }
        ["woken_up"]=>
        bool(true)
      }
    }
    ["unserialized"]=>
    bool(true)
  }
}
object(Wakeup)#5 (2) {
  ["data"]=>
  array(1) {
    [0]=>
    object(Unserialize)#6 (2) {
      ["data"]=>
      array(1) {
        [0]=>
        object(Wakeup)#7 (2) {
          ["data"]=>
          array(1) {
            [0]=>
            object(Unserialize)#8 (2) {
              ["data"]=>
              array(0) {
              }
              ["unserialized"]=>
              bool(true)
            }
          }
          ["woken_up"]=>
          bool(true)
        }
      }
      ["unserialized"]=>
      bool(true)
    }
  }
  ["woken_up"]=>
  bool(true)
}
