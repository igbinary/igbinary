--TEST--
Check for serialization handler, SessionHandlerInterface
--SKIPIF--
<?php
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
--FILE--
<?php
// https://github.com/igbinary/igbinary/issues/23
// http://www.php.net/manual/en/class.sessionhandlerinterface.php
$output = '';

class S implements SessionHandlerInterface {
    public function open($path, $name): bool {
        return true;
    }

    public function close(): bool {
        return true;
    }

    public function read($id): string {
        global $output;
        $output .= "read\n";
        return pack('H*', '0000000214011103666f6f0601');
    }

    public function write($id, $data): bool {
        global $output;
        $output .= "wrote: ";
        $output .= substr(bin2hex($data), 8). "\n";
        return true;
    }

    public function destroy($id): bool {
        return true;
    }

    // > Returns the number of deleted sessions on success, or false on failure.
    // > Note this value is returned internally to PHP for processing.
    public function gc($time): int {
        return 0;
    }
}

class Foo {
}

class Bar {
}

ini_set('session.serialize_handler', 'igbinary');

$handler = new S();
@session_set_save_handler($handler, true);

$db_object = new Foo();
$session_object = new Bar();

$v = session_start();
var_dump($v);
$_SESSION['test'] = "foobar";

session_write_close();

echo $output;

/*
 * you can add regression tests for your extension here
 *
 * the output of your test code has to be equal to the
 * text in the --EXPECT-- section below for the tests
 * to pass, differences between the output and the
 * expected text are interpreted as failure
 *
 * see TESTING.md for further information on
 * writing regression tests
 */
?>
--EXPECT--
bool(true)
read
wrote: 14021103666f6f06011104746573741106666f6f626172
