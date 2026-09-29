--TEST--
Objects serialized as null don't shift the ids of later references
--INI--
error_reporting=E_ALL & ~E_DEPRECATED
--FILE--
<?php
class SerializesAsNull implements Serializable {
    public function serialize() { return null; }
    public function unserialize($data) {}
}
class SleepsAsNull {
    public function __sleep() { return null; }
}
$n = new SerializesAsNull();
$b = new SleepsAsNull();
$o = new stdClass();
$data = [$n, $o, $n, &$b, $o, &$b, $b, $o, $n];
$u = igbinary_unserialize(igbinary_serialize($data));
var_dump($u);
var_dump($u[1] === $u[4] && $u[4] === $u[7]);
$u[3] = 'shared';
var_dump($u[5]);
?>
--EXPECTF--
%s: igbinary_serialize(): SleepsAsNull::__sleep() should return an array only containing the names of instance-variables to serialize in %s on line %d
array(9) {
  [0]=>
  NULL
  [1]=>
  object(stdClass)#%d (0) {
  }
  [2]=>
  NULL
  [3]=>
  &NULL
  [4]=>
  object(stdClass)#%d (0) {
  }
  [5]=>
  &NULL
  [6]=>
  NULL
  [7]=>
  object(stdClass)#%d (0) {
  }
  [8]=>
  NULL
}
bool(true)
string(6) "shared"
