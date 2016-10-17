--TEST--
Test serializing more strings than the capacity of the initial strings table.
--SKIPIF--
--FILE--
<?php
function main() {
	$array = array();
	for ($i = 0; $i < 2; $i++) {
		for ($c = 'a'; $c < 'z'; $c++) {
			$array[] = $c;
		}
	}
	$serialized = igbinary_serialize($array);
	printf("%s\n", bin2hex(substr($serialized, 4)));
	$unserialized = igbinary_unserialize($serialized);
	echo implode(',', $unserialized);
}
main();
?>
--EXPECT--
1432060011016106011101620602110163060311016406041101650605110166060611016706071101680608110169060911016a060a11016b060b11016c060c11016d060d11016e060e11016f060f11017006101101710611110172061211017306131101740614110175061511017606161101770617110178061811017906190e00061a0e01061b0e02061c0e03061d0e04061e0e05061f0e0606200e0706210e0806220e0906230e0a06240e0b06250e0c06260e0d06270e0e06280e0f06290e10062a0e11062b0e12062c0e13062d0e14062e0e15062f0e1606300e1706310e18
a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y
