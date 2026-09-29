--TEST--
__serialize() mechanism (015): Uninitialized typed properties returned by __sleep are skipped when serializing
--SKIPIF--
<?php
if (PHP_VERSION_ID < 70400) { echo "skip __serialize/__unserialize not supported in php < 7.4 for compatibility with igbinary_serialize()"; }
?>
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
error_reporting(E_ALL);
set_error_handler(function ($errno, $message) {
    echo $message . "\n";
});
class OSI {
    public stdClass $o;
    public string $s;
    public ?int $i;
    public float $f;
    public function __sleep() {
        return ['o', 's', 'i'];
    }
}
class SimplePublic {
    public ?int $i;
    public function __sleep() {
        return ['i'];
    }
}
class SimpleProtected {
    protected ?int $i;
    public function __sleep() {
        return ['i'];
    }
    public function __set($name, $value) {
        $this->$name = $value;
    }
}
class SimplePrivate {
    private ?int $i;
    public function __sleep() {
        return ['i'];
    }
    public function __set($name, $value) {
        $this->$name = $value;
    }
}
// 00000002               -- header
// 17 03 4d79436c617373   -- object of type "MyClass"
//   14 03 000000           -- with 3 uninitialized properties
$m = new OSI();
function try_serialize_invalid($o) {
    try {
        var_dump(bin2hex($s = igbinary_serialize($o)));
    } catch (Error $e) {
        printf("Caught %s: %s\n", get_class($e), $e->getMessage());
    }
}
// These are skipped whether or not the uninitialized property is nullable, like serialize() does.
try_serialize_invalid(new OSI());
try_serialize_invalid(new SimplePublic());
try_serialize_invalid(new SimpleProtected());
try_serialize_invalid(new SimplePrivate());
$s = new SimplePublic();
$s->i = null;
try_serialize_invalid($s);
$s = new SimpleProtected();
$s->i = 0;
try_serialize_invalid($s);
$s = new SimplePrivate();
$s->i = null;
try_serialize_invalid($s);

--EXPECT--
string(22) "0000000217034f53491400"
string(40) "00000002170c53696d706c655075626c69631400"
string(46) "00000002170f53696d706c6550726f7465637465641400"
string(42) "00000002170d53696d706c65507269766174651400"
string(48) "00000002170c53696d706c655075626c6963140111016900"
string(62) "00000002170f53696d706c6550726f74656374656414011104002a00690600"
string(80) "00000002170d53696d706c6550726976617465140111100053696d706c6550726976617465006900"
