--TEST--
igbinary_unserialize should never convert from packed array to hash when references exist (Bug #48)
--SKIPIF--
--FILE--
<?php
function main() {
	$result = array();
	// The default hash size is 16. If we start with 0, and aren't careful, the array would begin as a packed array,
	// and the references would be invalidated when key 50 (>= 16) is added (converted to a hash), causing a segfault.
	foreach (array(0, 50) as $i) {
	    $inner = new stdClass();
	    $inner->a = $i;
	    $result[0][0][$i] = $inner;
	    $result[1][] = $inner;
	}
	$serialized = igbinary_serialize($result);
	printf("%s\n", bin2hex(substr($serialized, 4)));
	flush();
	$unserialized = igbinary_unserialize($serialized);
	var_dump($unserialized);
}
main();
?>
--EXPECTF--
1402060014010600140206001708737464436c6173731401110161060006321a0014010e010632060114020600220306012204
array(2) {
  [0]=>
  array(1) {
    [0]=>
    array(2) {
      [0]=>
      object(stdClass)#%d (1) {
        ["a"]=>
        int(0)
      }
      [50]=>
      object(stdClass)#%d (1) {
        ["a"]=>
        int(50)
      }
    }
  }
  [1]=>
  array(2) {
    [0]=>
    object(stdClass)#%d (1) {
      ["a"]=>
      int(0)
    }
    [1]=>
    object(stdClass)#%d (1) {
      ["a"]=>
      int(50)
    }
  }
}
