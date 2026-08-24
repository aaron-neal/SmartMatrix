# SmartMatrix tests

`extras/` is ignored by the Arduino IDE, so nothing here affects normal use of
the library.

## Host unit tests

Pure arithmetic, no hardware, roughly one second to run.

    powershell -File extras/test/host/run.ps1

Expected output: `ALL TESTS PASSED`.

Requires a C++17 compiler. The script points at
`C:\Program Files\LLVM\bin\clang++.exe`; edit `$clang` if yours is elsewhere.

## ESP32 compile check

Confirms the library still compiles for the target hardware at a given refresh
depth. No board needs to be attached.

    cd extras/test/compile-check
    pio run -e depth24        # the shipping default
    pio run -e depth12        # any of depth6/9/12/18/24/36/48

`bad20`, `bad3` and `bad51` are envs that are *expected to fail*, proving the
compile-time guard rejects a depth that is not a multiple of 3, is below 6, or
is above 48. A bare `pio run` with no `-e` builds every env including those
three, so always pass `-e`.

Supported depths are multiples of 3 from 6 to 48. Anything else is a compile
error naming the valid range.

The `platform` pin in `platformio.ini` is deliberate — the unpinned
`espressif32` can resolve to a dev snapshot that fails before compiling.
