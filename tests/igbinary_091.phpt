--TEST--
Check for handling of anonymous classes
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
function check_serialize_throws($obj) {
    try {
        var_dump(serialize($obj));
    } catch (Throwable $e) {
        echo "Caught: " . $e->getMessage() . "\n";
    }
}
check_serialize_throws(new class () {});
// TODO: Update behavior based on https://bugs.php.net/bug.php?id=81111
/**
check_serialize_throws(new class () {
    public function __serialize() { return []; }
    public function __unserialize($value) { }
});
 */
check_serialize_throws(new class () implements Serializable {
    public function serialize() { return ''; }
    public function unserialize($ser) { return new self(); }
});
?>
--EXPECTF--
Caught: Serialization of 'class@anonymous%S' is not allowed
Caught: Serialization of '%s@anonymous%S' is not allowed
