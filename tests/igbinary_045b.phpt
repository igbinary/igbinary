--TEST--
APCu serializer registration
--SKIPIF--
<?php
if (!extension_loaded('apcu')) {
	echo "skip APCu not loaded";
}

$ext = new ReflectionExtension('apcu');
if (version_compare($ext->getVersion(), '4.0.2', '<')) {
	echo "skip require APCu version 4.0.2 or above";
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
