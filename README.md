igbinary
========

[![Build Status](https://github.com/igbinary/igbinary/actions/workflows/main.yml/badge.svg?branch=master)](https://github.com/igbinary/igbinary/actions/workflows/main.yml?query=branch%3Amaster)
[![Build Status](https://travis-ci.org/igbinary/igbinary.svg?branch=v3)](https://travis-ci.org/igbinary/igbinary)
[![Build Status (Windows)](https://ci.appveyor.com/api/projects/status/suhkkumj1yh9dgan?svg=true)](https://ci.appveyor.com/project/TysonAndre/igbinary-bemsx)

Igbinary is a drop in replacement for the standard php serializer.
Instead of the time and space consuming textual representation used by PHP's `serialize`,
igbinary stores php data structures in a compact binary form.
Memory savings are significant when using memcached, APCu, or similar memory based storages for serialized data.
The typical reduction in storage requirements are around 50%.
The exact percentage depends on your data.

Unserialization performance is [at least on par with the standard PHP serializer, and is much faster for repetitive data](benchmark/comparisons.php).
Serialization performance depends on the `igbinary.compact_strings` option which enables
duplicate string tracking.
String are inserted to a hash table, which adds some overhead when serializing.
In usual scenarios this does not have much of an impact,
because the typical usage pattern is "serialize rarely, unserialize often".
With the `compact_strings` option enabled,
igbinary is usually a bit slower than the standard serializer.
Without it, igbinary is a bit faster.

Features
--------

- Support for the same data types as the standard PHP serializer: null, bool, int,
  float, string, array and object.
- `__autoload` & `unserialize_callback_func`
- `__sleep` & `__wakeup`
- `__serialize` & `__unserialize` (only used in php 7.4+)
- Serializable -interface
- Data portability between platforms (32/64bit, endianness)
- Tested on Linux amd64, Linux ARM, Mac OSX x86, HP-UX PA-RISC and NetBSD sparc64
- Hooks up to the APCu in-memory key-value store as a serialization handler.
- Compatible with 7.0 &ndash; 8.0 (The older igbinary [2.x releases](https://github.com/igbinary/igbinary/tree/v2) support 5.2 &ndash; 5.6, 7.0 &ndash; 7.3)

Implementation details
----------------------

Storing complex PHP data structures such as arrays of associative arrays
with the standard PHP serializer is not very space efficient.
The main reasons of this inefficiency are listed below, in order of significance (at least in our applications):

1. Array keys, property names, and class names are repeated redundantly.
2. Numerical values are plain text.
3. Human readability adds some overhead.

Igbinary uses two strategies to minimize the size of the serialized
output.

1. Repeated strings are stored only once (this also includes class and property names).
   Collections of objects benefit significantly from this.
   See the `igbinary.compact_strings` option.

2. Integer values are stored in the smallest primitive data type available:
    *123* = `int8_t`,
    *1234* = `int16_t`,
    *123456* = `int32_t`
 ... and so on.

3. ( Well, it is not human readable ;)

How to use
----------

Add the following lines to your php.ini:

```ini
; Load igbinary extension
extension=igbinary.so

; Use igbinary as session serializer
session.serialize_handler=igbinary

; Enable or disable compacting of duplicate strings
; The default is On.
igbinary.compact_strings=On

; If uncommented, use igbinary as the serializer of APCu
; (APCu 5.1.10 or newer is strongly recommended)
;apc.serializer=igbinary
```

Then, in your php code, replace `serialize` and `unserialize` function calls
with [`igbinary_serialize` and `igbinary_unserialize`](./igbinary.php).

Installing
----------

### Linux

If PHP was installed through your package manager,
the package manager may also contain prebuilt packages for `igbinary`
(with a package name similar to php-igbinary).

- The packages from some package managers and OS versions may be out of date and have known bugs. The latest release of igbinary is [![The Latest Stable Version](https://img.shields.io/github/v/release/igbinary/igbinary.svg)](https://github.com/igbinary/igbinary/releases)

Igbinary may also be installed with the command `pecl install igbinary` (You will need to enable igbinary in php.ini)

Alternately, you may wish to [build from source](#building-from-source)

### MacOS

`pecl install igbinary` is the recommended installation method (You will need to enable igbinary in php.ini)

Alternately, you may wish to [build from source](#building-from-source).

### Installing on Windows

Prebuilt DLLs can be [downloaded from PECL](https://pecl.php.net/package/igbinary).

If you are a contributor to/packager of igbinary, or need to build from source, see [WINDOWS.md](./WINDOWS.md)

### Building from source

1. `phpize`
2. `./configure`

    - With GCC: `./configure CFLAGS="-O2 -g" --enable-igbinary`
    - With ICC (Intel C Compiler) `./configure CFLAGS=" -no-prec-div -O3 -xO -unroll2 -g" CC=icc --enable-igbinary`
    - With clang: `./configure CC=clang CFLAGS="-O0 -g" --enable-igbinary`
3. `make`
4. `make test`
5. `make install`
6. `igbinary.so` is installed to the default extension directory

Bugs & Contributions
--------------------

Mailing list for bug reports and other development discussion can be found
at http://groups.google.com/group/igbinary (no longer used)

File bug reports at
https://github.com/igbinary/igbinary/issues

The preferred way to contribute is with pull requests.
Feel free to fork this at http://github.com/igbinary/igbinary

See [TESTING.md](./TESTING.md) for advice for testing patches.

See [TECH\_NOTES.md](./TECH_NOTES.md) for information about how igbinary is implemented

Utilizing in other extensions
-----------------------------

Igbinary can be called from other extensions fairly easily. Igbinary installs
its header file to _ext/igbinary/igbinary.h_. There are just two straightforward
functions: `igbinary_serialize` and `igbinary_unserialize`. Look at _igbinary.h_ for
prototypes and usage.

Add `PHP_ADD_EXTENSION_DEP(yourextension, igbinary)` to your _config.m4_ in case
someone wants to compile both of them statically into php.

Trivia
------

Where does the name "igbinary" come from? There was once a similar project
called fbinary but it has disappeared from the Internet a long time ago. Its
architecture wasn't particularly clean either. IG is an abbreviation for a
Finnish social networking site IRC-Galleria (http://irc-galleria.net/)
