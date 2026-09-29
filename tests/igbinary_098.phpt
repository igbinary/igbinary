--TEST--
Test PHP 8.2 readonly classes
--SKIPIF--
<?php
if (!extension_loaded("igbinary")) print "skip\n";
if (PHP_VERSION_ID < 80200) print "skip php < 8.2\n";
?>
--FILE--
<?php
readonly class C {
    public int $a;
    public static function create() {
        $c = new C();
        $c->a = 8;
        return $c;
    }
}
$c = new C();
$ser = igbinary_serialize($c);
echo urlencode($ser), "\n";
var_dump(igbinary_unserialize($ser));
$c = C::create();
$ser = igbinary_serialize($c);
echo urlencode($ser), "\n";
var_dump(igbinary_unserialize($ser));
$invalid1 = str_replace('a', 'b', $ser);
try {
    var_dump(igbinary_unserialize($invalid1));
} catch (Error $e) {
    printf("%s: %s\n", $e::class, $e->getMessage());
}
--EXPECT--
%00%00%00%02%17%01C%14%00
object(C)#2 (0) {
  ["a"]=>
  uninitialized(int)
}
%00%00%00%02%17%01C%14%01%11%01a%06%08
object(C)#1 (1) {
  ["a"]=>
  int(8)
}
Error: Cannot create dynamic property C::$b in igbinary_unserialize
