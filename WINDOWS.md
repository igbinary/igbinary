Compiling igbinary from source on Windows
=========================================

This guide is added for packagers or developers of the igbinary project.

Installing through PECL (Not available yet) is probably easier for users of igbinary?

Setting up a Windows VM
-----------------------

If you don't have Windows installed:

1. Download a VM from https://developer.microsoft.com/en-us/microsoft-edge/tools/vms/ (E.g. Windows 7 IE11)
   Prefer Windows 7. The trial period can be extended.
2. Install the VM. Keep the disk image around, the VM trial period expires in 30 days.
3. (Optional) Configure the VM to use more than one core (but not all available cores) to speed up installation/upgrading/compiling.
4. Download https://support.microsoft.com/en-us/kb/3102810#bookmark-prerequisite (x86), disable internet, install that patch, re-enable internet)
5. Run Windows Updates, which will take a long time.
6. Read the instructions on the desktop background (for extending trial period).
7. (Windows 7)Search for "Command Prompt", right click on it and click "Run as administrator", and execute `slmgr /ato` to extend the trial period to 90 days.

If the license expires/is about to expire, it is possible to "re-arm" (extend) the license several times with `slmgr`

Installing Visual Studio
------------------------

1. Download the version of Visual Studio needed to build the desired version of PHP (mentioned on https://wiki.php.net/internals/windows/stepbystepbuild#building_pecl_extensions)
   At the time of writing, that was [Visual Studio Community Edition 2015](https://www.visualstudio.com/products/visual-studio-community-vs) for PHP7.
   Visual Studio 2015 **is not able to compile php 5**. Install Visual Studio 2012 if building php 5.5 or 5.6. Install Visual Studio 2008 if installing php 5.4.
2. Install Visual Studio 2015. (Use custom installation, make sure that all C++ features are enabled)

Downloading PHP
---------------

See https://wiki.php.net/internals/windows/stepbystepbuild

Prefer downloading stable releases of php?

(This hasn't been tested yet on the suggested VM)


Building igbinary
-----------------

See https://wiki.php.net/internals/windows/stepbystepbuild#building\_pecl\_extensions

### Install APCU (optional)

Note: The Windows build hasn't worked for a while in 5.6, so there may be compilation errors or test failures.

TODO: Make sure that `config.w32` works without APCU installed.

> 1. Open the extension's page on PECL ([APCu](https://pecl.php.net/package/APCu))
> 2. Download the extension source either by:
>
>    -    downloading a source archive
>    -    fetching the source from the extension's repository (link can be found under *Browse Source*)
> 3. Create a directory named pecl on the same level as your PHP source directory, e.g. C:\php-sdk\phpdev\vc11\x86\pecl
> 4. Extract or clone the extension source code to the pecl directory
>
>    - if cloning, clone to a subdirectory, e.g. C:\php-sdk\phpdev\vc11\x86\pecl\apcu
>    - source code archive should already contain a subdirectory named e.g. apcu-4.0.7
> 5. Open a command prompt, run the `setvars` script, and enter your PHP source directory
> 6. Rebuild the configure script by running:
>
>    ```
>    buildconf
>    ```
>
> 7. Executing `configure --help` should now contain an option to enable APCu
>
>    ```
>    --enable-apcu    Whether to enable APCu support
>    ```
>
> 8. Configure and build:
>
>    ```
>    configure --disable-all --enable-cli --enable-apcu
>    ```
>
>    ```
>    nmake
>    ```
>
> 9. Test the binary with a php -m command, to make sure APCu is loaded

### Install igbinary

Similar to the above instructions.

1. (This isn't released yet, skip this step and download from github)
   Open the extension's page on PECL ( APCu)
2. Download the extension source either by:

   -    downloading a source archive
   -    fetching the source from the extension's repository (link can be found under *Browse Source*)
        https://github.com/igbinary/igbinary/
3. If it doesn't already exist, then create a directory named pecl on the same level as your PHP source directory, e.g. C:\php-sdk\phpdev\vc11\x86\pecl
4. Extract or clone the extension source code to the pecl directory

   - if cloning, clone to a subdirectory, e.g. C:\php-sdk\phpdev\vc11\x86\pecl\igbinary
   - source code archive should already contain a subdirectory named e.g. igbinary-2.0.0. That should be renamed to igbinary.
5. Open a command prompt, run the setvars script, and enter your PHP source directory (not the extension directory)
6. Rebuild the configure script by running:

   ```
   buildconf
   ```

7. Executing `configure --help` should now contain options.
   (`config.m4` currently has no options. May allow disabling apcu support in the future, etc.)

   ```
   --enable-apcu    Whether to enable APCu support
   --enable-igbinary Whether to enable igbinary support
   ```

8. Configure and build:
   One may want to set CFLAGS (or equivalent) to -O2 so it will be faster?

   If you want to test sessions and apcu as well, an extremely minimal configure script would be:

   ```
   configure --disable-all --enable-cli --enable-cgi --enable-apcu --enable-igbinary --enable-json
   ```

   (If testing a full installation, add `--enable-igbinary` to the config flags you were already using)

   Then, build/rebuild php.

   ```
   nmake
   ```

9. Test the binary with a php -m command, to make sure igbinary is loaded. (and any other extensions such as APCu)
10. Run the unit tests. Most should pass, some should be skipped (Or be expected failures(XFAIL)), none should fail.

    ```
    # In the php source directory, execute the following commands:
    nmake test TESTS=C:\php-sdk\phpdev\path\to\igbinary\tests
    # Optional test of related extensions:
    # nmake test TESTS=C:\php-sdk\phpdev\path\to\apcu\tests
    ```

    In recent releases of php7, this can be probably done by running `nmake test`
	within the igbinary folder immediately after configuration for the desired php7 version.
