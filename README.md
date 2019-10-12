# CPThreads

This project implements the C11 threading header `threads.h` for Windows. You can include it in projects that target windows, linux, MacOS, etc, and as long as the compiler is either MSVC (cl.exe) or a compiler that implements threads.h, the standard threading functions will be available.

It is designed to function as closely to the standard as possible, but it's not perfect. In particular, `thrd_sleep` only has millisecond accuracy by default, and its second parameter is never set. Also, the `cnd_*` functions are currently untested, and probably won't work with timed mutex types, but should work as expected with both plain mutex types.

# Building

This project uses meson by default, but as the main part only consists of two files with few options, it should be easy to adapt to any build system. An example meson build:

```sh
git clone https://github.com/precisamento/cpthreads
cd cpthreads
mkdir build
cd build
meson .. --buildtype=release
ninja
```

# Testing

The tests are created with the [check]() unit testing library. To build the test project (Windows only) just set the check_location option. The default install location is `C:\Program Files (x86)\check`, if you can't find it.

```sh
meson configure "-Dcheck_location=path/to/check location"
```

# Other Options

To get a more accurate sleep function you can define CP_ACCURATE_SLEEP. This version of the function is untested, but gives accuracy at the 100 nanosecond level.

Unless you're building this for a final exe, this is not recommended because the header requires the define, so consumers would also have to define it.