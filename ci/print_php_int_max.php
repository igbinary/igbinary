#!/usr/bin/env php
<?php
printf("PHP Integer Maximum is actually : %d (%d-bit)\n",
       PHP_INT_MAX,
       (PHP_INT_MAX === 0x7fffffff) ? 32 : 64);
