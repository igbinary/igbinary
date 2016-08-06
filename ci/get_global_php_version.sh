#!/bin/sh
php -r "echo(preg_replace('/-dev|RC[0-9]+$/','',PHP_VERSION));"
