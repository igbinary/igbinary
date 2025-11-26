--TEST--
igbinary session decoder should call __wakeup
--INI--
date.timezone=UTC
session.serialize_handler=igbinary
error_reporting=E_ALL & ~E_DEPRECATED
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

--FILE--
<?php

class A {
	public $b;
	public $c;
	public function __wakeup() {
		$this->c = $this->b . 'OnWakeup';
	}
}

error_reporting(E_ALL);
// Start session
session_start();
// The serialization of DateTime varies in php5 and php 7.
$_SESSION['date'] = new \DateTime('@1234567890');
$a = new A();
$a->b = 'value';
$_SESSION['a'] = $a;
var_dump($_SESSION['date']->getTimestamp());
$mydata = session_encode();
unset($_SESSION['date']);
var_dump(session_decode($mydata));
var_dump($_SESSION['date']->getTimestamp());
var_dump($_SESSION['a']->c);
?>
--EXPECT--
int(1234567890)
bool(true)
int(1234567890)
string(13) "valueOnWakeup"
