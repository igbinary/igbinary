--TEST--
Check for serialization handler, SessionHandlerInterface
--SKIPIF--
<?php
// http://php.net/manual/en/class.sessionhandlerinterface.php (PHP 5 >= 5.4.0)
if (version_compare(phpversion(), "5.4.0", "<")) {
    exit("skip php version less than 5.4.x");
}

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


--FILE--
<?php
// https://github.com/igbinary/igbinary/issues/23
// http://www.php.net/manual/en/class.sessionhandlerinterface.php
$output = '';

class S implements SessionHandlerInterface {
    public function open($path, $name) {
        return true;
    }

    public function close() {
        return true;
    }

    public function read($id) {
        global $output;
        $output .= "read\n";
        return pack('H*', '0000000214011103666f6f0601');
    }

    public function write($id, $data) {
        global $output;
        $output .= "wrote: ";
        $output .= substr(bin2hex($data), 8). "\n";
        return true;
    }

    public function destroy($id) {
        return true;
    }

    public function gc($time) {
        return true;
    }
}

class Foo {
}

class Bar {
}

ini_set('session.serialize_handler', 'igbinary');

$handler = new S();
session_set_save_handler($handler, true);

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
 * see php5/README.TESTING for further information on
 * writing regression tests
 */
?>
--EXPECT--
bool(true)
read
wrote: 14021103666f6f06011104746573741106666f6f626172
