igbinary
========

[![Build Status](https://travis-ci.org/igbinary/igbinary.svg?branch=master)](https://travis-ci.org/igbinary/igbinary)

Igbinary is a drop in replacement for the standard php serializer. Instead of
time and space consuming textual representation, igbinary stores php data
structures in compact binary form. Savings are significant when using
memcached or similar memory based storages for serialized data. About 50%
reduction in storage requirement can be expected. Specific number depends on
your data.

Unserialization performance is at least on par with the standard PHP serializer.
Serialization performance depends on the "compact_strings" option which enables
duplicate string tracking. String are inserted to a hash table which adds some
overhead. In usual scenarios this does not have much significance since usage
pattern is "serialize rarely, unserialize often". With "compact_strings"
option igbinary is usually a bit slower than the standard serializer. Without
it, a bit faster.

Features
--------

- Supports same data types as the standard PHP serializer: null, bool, int,
  float, string, array and objects.
- `__autoload` & `unserialize_callback_func`
- `__sleep` & `__wakeup`
- Serializable -interface
- Data portability between platforms (32/64bit, endianess)
- Tested on Linux amd64, Linux ARM, Mac OSX x86, HP-UX PA-RISC and NetBSD sparc64
- Hooks up to APC opcode cache as a serialization handler (APC 3.1.7+)
- Compatible with PHP 5.2 &ndash; 5.6

Implementation details
----------------------

Storing complex PHP data structures like arrays of associative arrays
with the standard PHP serializer is not very space efficient. The main
reasons in order of significance are (at least in our applications):

1. Array keys are repeated redundantly.
2. Numerical values are plain text.
3. Human readability adds some overhead.

Igbinary uses two specific strategies to minimize the size of the serialized
output.

1. Repetitive strings are stored only once. Collections of objects benefit
   significantly from this. See "compact_strings" option.

2. Numerical values are stored in the smallest primitive data type
   available:
    *123* = `int8_t`,
    *1234* = `int16_t`,
    *123456* = `int32_t`
 ... and so on.

3. ( Well, it is not human readable ;)

How to use
----------

Add the following lines to your php.ini:

    ; Load igbinary extension
    extension=igbinary.so

    ; Use igbinary as session serializer
    session.serialize_handler=igbinary

    ; Enable or disable compacting of duplicate strings
    ; The default is On.
    igbinary.compact_strings=On

    ; Use igbinary as serializer in APC cache (3.1.7 or later)
    ;apc.serializer=igbinary

.. and in your php code replace serialize and unserialize function calls
with `igbinary_serialize` and `igbinary_unserialize`.

Installing
----------

Note:
Sometimes phpize must be substituted with phpize5. In such cases the following
option must be given to configure script: "--with-php-config=.../php-config5"

1. `phpize`
2. `./configure:
    - With GCC: `./configure CFLAGS="-O2 -g" --enable-igbinary`
    - With ICC (Intel C Compiler) `./configure CFLAGS=" -no-prec-div -O3 -xO -unroll2 -g" CC=icc --enable-igbinary`
    - With clang: `./configure CC=clang CFLAGS="-O0 -g" --enable-igbinary`
3. `make`
4. `make test`
5. `make install`
6. igbinary.so is installed to the default extension directory

### To run APCu test

```
# go to modules directory
cd modules

# ... and create symlink to apcu extension
# it will be loaded during test suite
/opt/lib/php/extensions/no-debug-non-zts-20121212/apcu.so
```

Similar approach should work for APC.

### Installing on Windows

If you are a contributor to/packager of igbinary, see [WINDOWS.md](./WINDOWS.md)

Bugs & Contributions
--------------------

Mailing list for bug reports and other development discussion can be found
at http://groups.google.com/group/igbinary

Fill bug reports at
https://github.com/igbinary/igbinary/issues

The preferred ways for contributions are pull requests and email patches
(in git format). Feel free to fork at http://github.com/igbinary/igbinary

Utilizing in other extensions
-----------------------------

Igbinary can be called from other extensions fairly easily. Igbinary installs
its header file to _ext/igbinary/igbinary.h_. There are just two straighforward
functions: `igbinary_serialize` and `igbinary_unserialize`. Look at _igbinary.h_ for
prototypes and usage.

Add `PHP_ADD_EXTENSION_DEP(yourextension, igbinary)` to your _config.m4_ in case
someone wants to compile both of them statically into php.

Trivia
------

Where does the name "igbinary" come from? There was once a similar project
called fbinary but it has disappeared from the Internet a long time ago. Its
architecture wasn't particularly clean either. IG is an abbreviation for a
finnish social networking site IRC-Galleria (http://irc-galleria.net/)


