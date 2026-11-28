--TEST--
igbinary_serialize(): a hook that grows the array being walked through a reference must not free it
--SKIPIF--
<?php if (PHP_VERSION_ID < 70400) { echo "skip requires __serialize\n"; } ?>
--FILE--
<?php
class G {
    public $ref;
    public function __serialize(): array {
        for ($i = 0; $i < 128; $i++) {
            $this->ref[] = 'x' . $i;
        }
        return ['d' => 1];
    }
    public function __unserialize(array $data): void {
        var_dump($data);
    }
}
$g = new G();
$inner = [$g, 'tail'];
$g->ref = &$inner;
$top = [&$inner];
var_dump(igbinary_unserialize(igbinary_serialize($top)));
?>
--EXPECT--
array(1) {
  ["d"]=>
  int(1)
}
array(1) {
  [0]=>
  array(2) {
    [0]=>
    object(G)#2 (1) {
      ["ref"]=>
      NULL
    }
    [1]=>
    string(4) "tail"
  }
}
