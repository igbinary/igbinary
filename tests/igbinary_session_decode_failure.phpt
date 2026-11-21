--TEST--
igbinary session decoder must not leak the partially unserialized session array when the data is invalid
--SKIPIF--
<?php
if (!extension_loaded("igbinary")) print "skip";
if (!extension_loaded('session')) {
	exit('skip session extension not loaded');
}

ob_start();
phpinfo(INFO_MODULES);
$str = ob_get_clean();

$array = explode("\n", $str);
$array = preg_grep('/^igbinary session support.*yes/', $array);
if (!$array) {
	exit('skip igbinary session handler not available');
}
?>
--INI--
session.serialize_handler=igbinary
session.use_cookies=0
session.cache_limiter=
--FILE--
<?php
class NullHandler implements SessionHandlerInterface {
    public function open(string $path, string $name): bool { return true; }
    public function close(): bool { return true; }
    public function read(string $id): string|false { return ''; }
    public function write(string $id, string $data): bool { return true; }
    public function destroy(string $id): bool { return true; }
    public function gc(int $max_lifetime): int|false { return 0; }
    public function create_sid(): string { return 'a'; }
    public function validateId(string $sid) { return true; }
}

session_set_save_handler(new NullHandler(), false);

function decode(string $data): void {
    session_id('igbinaryclaudetest');
    var_dump(session_start());
    var_dump(session_decode($data));
    var_dump($_SESSION);
    if (session_status() === PHP_SESSION_ACTIVE) {
        session_write_close();
    }
    $_SESSION = [];
}

// array8 n=2: "k" => [0 => "hello", 1 => "world", <missing 3rd element>]: the inner array is truncated before its 3rd key.
// The outer array has already been allocated when the decoder gives up.
decode("\x00\x00\x00\x02\x14\x02" . "\x11\x01k" . "\x14\x03\x06\x00\x11\x05hello\x06\x01\x11\x05world");

// array8 n=2: "k" => "hello", then end-of-data where the 2nd value should be.
decode("\x00\x00\x00\x02\x14\x02" . "\x11\x01k" . "\x11\x05hello" . "\x11\x01j");

// Truncated before anything was built: nothing to free.
decode("\x00\x00\x00\x02\x14");

// A valid payload followed by a stray byte: rejected like igbinary_unserialize() does, nothing leaked.
decode(igbinary_serialize(['k' => 'v']) . "\x00");

// A valid payload still works afterwards.
decode(igbinary_serialize(['k' => 'v']));
echo "Done\n";
--EXPECTF--
bool(true)

Warning: igbinary_unserialize_array: end-of-data in %s on line %d

Warning: session_decode(): Failed to decode session object. Session has been destroyed in %s on line %d
bool(false)
array(0) {
}
bool(true)

Warning: igbinary_unserialize_zval: end-of-data in %s on line %d

Warning: session_decode(): Failed to decode session object. Session has been destroyed in %s on line %d
bool(false)
array(0) {
}
bool(true)

Warning: igbinary_unserialize_array: end-of-data in %s on line %d

Warning: session_decode(): Failed to decode session object. Session has been destroyed in %s on line %d
bool(false)
array(0) {
}
bool(true)

Warning: igbinary_unserialize: received more data to unserialize than expected in %s on line %d

Warning: session_decode(): Failed to decode session object. Session has been destroyed in %s on line %d
bool(false)
array(0) {
}
bool(true)
bool(true)
array(1) {
  ["k"]=>
  string(1) "v"
}
Done
