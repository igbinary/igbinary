--TEST--
APC serializer registration
--SKIPIF--
<?php
if (!extension_loaded('apc')) {
	echo "skip APC not loaded";
}

$ext = new ReflectionExtension('apc');
if (version_compare($ext->getVersion(), '3.1.7', '<')) {
	echo "skip require APC version 3.1.7 or above";
}

--INI--
apc.enable_cli=1
apc.serializer=igbinary
--FILE--
<?php
echo ini_get('apc.serializer'), "\n";

class Bar {
	public $foo = 10;
}

$a = new Bar;
apc_store('foo', $a);
unset($a);

var_dump(apc_fetch('foo'));
--EXPECTF--
igbinary
object(Bar)#%d (1) {
  ["foo"]=>
  int(10)
}
