--TEST--
Test serializing many different classes
--SKIPIF--
<?php if (!extension_loaded("igbinary")) print "skip"; ?>
--FILE--
<?php
$code = '';
// Test creating over 2**16 distinct names and deduplicating them
for ($i = 0; $i < 70000; $i += 100) {
    $code .= "class C$i{}\n";
}
eval($code);
$values = [];
for ($x = 0; $x < 2; $x++) {
    for ($i = 0; $i < 70000; $i++) {
        $name = "C$i";
        if ($i % 100 == 0) {
            $values[] = new $name();
        } else {
            $values[] = $name;
        }
    }
}
$ser = igbinary_serialize($values);
$unserialized = igbinary_unserialize($ser);
var_dump($unserialized == $values);
var_dump($values[0]);
var_dump($values[67800]);

?>
--EXPECT--
bool(true)
object(C0)#1 (0) {
}
object(C67800)#679 (0) {
}
