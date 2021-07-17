--TEST--
Test handling php 8.1 readonly properties
--SKIPIF--
<?php if (PHP_VERSION_ID < 80100) { echo "skip readonly properties require php 8.1+\n"; } ?>
--FILE--
<?php
class X {
    public readonly mixed $var;

    public function __construct(
        public readonly int $a,
        private readonly ArrayAccess&Countable $intersection,
        protected readonly ?string $default = null,
    ) {
        $this->var = $intersection;
    }
}

class Y {
    public readonly mixed $var;

    public function __construct(
        public readonly int $a,
        private readonly ArrayAccess&Countable $intersection,
        protected readonly ?string $default = null,
    ) {
        $this->var = $intersection;
    }

    public function __serialize(): array {
        return [
            'a' => $this->a,
            'intersection' => $this->intersection,
            'default' => $this->default,
            'var' => $this->var,
        ];
    }
    public function __unserialize(array $data) {
        [
            'a' => $this->a,
            'intersection' => $this->intersection,
            'default' => $this->default,
            'var' => $this->var,
        ] = $data;
    }
}

$ser = igbinary_serialize(new X(1, new ArrayObject()));
echo urlencode($ser), "\n";
var_dump(igbinary_unserialize($ser));
$ser = igbinary_serialize(new Y(1, new ArrayObject()));
echo urlencode($ser), "\n";
var_dump(igbinary_unserialize($ser));
?>
--EXPECT--
%00%00%00%02%17%01X%14%04%11%03var%17%0BArrayObject%14%04%06%00%06%00%06%01%14%00%06%02%14%00%06%03%00%11%01a%06%01%11%0F%00X%00intersection%22%01%11%0A%00%2A%00default%00
object(X)#1 (4) {
  ["var"]=>
  object(ArrayObject)#2 (1) {
    ["storage":"ArrayObject":private]=>
    array(0) {
    }
  }
  ["a"]=>
  int(1)
  ["intersection":"X":private]=>
  object(ArrayObject)#2 (1) {
    ["storage":"ArrayObject":private]=>
    array(0) {
    }
  }
  ["default":protected]=>
  NULL
}
%00%00%00%02%17%01Y%14%04%11%01a%06%01%11%0Cintersection%17%0BArrayObject%14%04%06%00%06%00%06%01%14%00%06%02%14%00%06%03%00%11%07default%00%11%03var%22%01
object(Y)#1 (4) {
  ["var"]=>
  object(ArrayObject)#2 (1) {
    ["storage":"ArrayObject":private]=>
    array(0) {
    }
  }
  ["a"]=>
  int(1)
  ["intersection":"Y":private]=>
  object(ArrayObject)#2 (1) {
    ["storage":"ArrayObject":private]=>
    array(0) {
    }
  }
  ["default":protected]=>
  NULL
}
