<?php

// Description: Unserialize scalar array

require_once 'bench.php';

call_user_func(function () {
    $b = new Bench('unserialize-scalar-array');

    $array = array();
    for ($i = 0; $i < 1000; $i++) {
        switch ($i % 4) {
        case 0:
            $array[] = "da string " . $i;
            break;
        case 1:
            $array[] = 1.31 * $i;
            break;
        case 2:
            $array[] = rand(0, PHP_INT_MAX);
            break;
        case 3:
            $array[] = (bool)($i & 1);
            break;
        }
    }
    $ser = igbinary_serialize($array);

    for ($i = 0; $i < 40; $i++) {
        $b->start();
        for ($j = 0; $j < 5800; $j++) {
            $array = igbinary_unserialize($ser);
        }
        $b->stop($j);
        $b->write();
    }
});
