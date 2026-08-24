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
    pio run -e depth12        # any of depth6/9/12/18/24/27/36/45/48
    pio run                   # builds all valid depths, no -e needed

`bad20`, `bad3` and `bad51` are envs that are *expected to fail*, proving the
compile-time guard rejects a depth that is not a multiple of 3, is below 6, or
is above 48. A bare `pio run` with no `-e` builds only the valid depths
(`default_envs` in `platformio.ini` excludes `bad20`/`bad3`/`bad51`); pass
`-e bad20` (etc.) explicitly to run one of those.

Supported depths are multiples of 3 from 6 to 48. Anything else is a compile
error naming the valid range.

That range applies to `SMARTMATRIX_ALLOCATE_BUFFERS`. The runtime-parameterised
`SMARTMATRIX_ALLOCATE_BUFFERS_NT` still supports only 24, 36 and 48 — it takes
its depth at runtime, so the compile-time guard cannot reach it, and an
unsupported depth there fails silently rather than at build time.

The `platform` pin in `platformio.ini` is deliberate — the unpinned
`espressif32` can resolve to a dev snapshot that fails before compiling.

## Known limitation: scrolling layers at reduced depth

`SM_Layer::setRefreshRate()` takes a `uint8_t`, but reduced refresh depths push
the calculated refresh rate above 255 (roughly 1683 Hz at `kRefreshDepth` 12).
The value truncates, so `SMLayerScrolling` and `SMLayerIndexed` compute their
scroll speed from a wrong refresh rate and scroll several times too fast.

Background-only sketches are unaffected. If you use a scrolling or indexed
layer below `kRefreshDepth` 24, set the scroll speed by eye, or stay at 24.
Fixing this properly means widening the layer refresh-rate type across all
platforms, which is deliberately out of scope for this change.
