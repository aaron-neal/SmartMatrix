# ESP32 Sub-24-bit Refresh Depth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow `kRefreshDepth` values below 24 on the ESP32 HUB75 path, freeing roughly 31 KB of DMA-capable RAM at `kRefreshDepth` 12.

**Architecture:** The bit-plane extraction arithmetic is currently a hardcoded if-ladder duplicated in two functions. Extract it into one dependency-free header that can be unit-tested on the host, replace both ladders with calls to it, widen the depth dispatch that currently only recognises exact values of 8/12/16 planes, and add a compile-time guard so an invalid depth is a build error rather than a silently corrupt display.

**Tech Stack:** C++ headers (Arduino library, no build system of its own), clang++ for host unit tests, PlatformIO for ESP32 compile verification, ESP32 serial boot log for on-hardware measurement.

**Spec:** `docs/superpowers/specs/2026-08-24-esp32-low-color-depth-design.md`

## Global Constraints

- **Existing depths must not change behaviour.** `kRefreshDepth` 24, 36, and 48 must produce byte-identical boot logs and visually identical output before and after every task. This is the primary regression gate.
- **Nothing is removed or restructured.** Teensy 3, Teensy 4, APA102, all pinouts, all panel types, and all layer types stay exactly as they are. This plan only adds and generalises.
- **The `_NT` (non-templated) ESP32 implementation is out of scope.** `MatrixEsp32Hub75Calc_NT_Impl.h` carries its own copies of the same ladders. It takes depth as a runtime argument, so a `static_assert` cannot apply to it. Leave it untouched; it continues to support 24/36/48 exactly as today.
- **Valid depth range after this work:** multiples of 3, from 6 (2 bit-planes) to 48 (16 bit-planes).
- **Target build for all hardware verification:** 64x32, `SMARTMATRIX_HUB75_32ROW_64COL_MOD8SCAN`, `GPIOPINOUT` 7 (`HUB75_ADAPTER_V0_SMT_PINOUT`, 8-bit I2S with latch), `ESP32_I2S_CLOCK_SPEED` 26.67 MHz.
- **Toolchain paths on this machine** (verified working):
  - clang++: `C:\Program Files\LLVM\bin\clang++.exe` (version 21.1.1)
  - PlatformIO: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` (version 6.1.19)
  - The PlatformIO platform **must** be pinned to `espressif32@6.9.0`. The unpinned `espressif32` resolves to a dev snapshot on this machine that crashes with `TypeError: argument should be a str or an os.PathLike object` before compiling anything.

## Baseline to preserve and beat

Measured from the boot log of the target build at `kRefreshDepth` 24:

| Quantity | Value |
| --- | --- |
| `sizeof framestruct` | `0x2100` (8,448 bytes) |
| Descriptor RAM | 24,576 bytes |
| `lsbMsbTransitionBit` | 0 of 7 |
| Refresh rate | 99 Hz |
| Total SmartMatrix DMA | 56,276 bytes |

Projected after the change:

| `kRefreshDepth` | planes | `sizeof framestruct` | Descriptor RAM | DMA total | Saved | Refresh |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 24 | 8 | 8,448 | 24,576 | 41,472 | -- | 99 Hz |
| 18 | 6 | 6,336 | 6,144 | 18,816 | 22,656 | 400 Hz |
| 12 | 4 | 4,224 | 1,536 | 9,984 | 31,488 | 1683 Hz |
| 9 | 3 | 3,168 | 768 | 7,104 | 34,368 | 3608 Hz |
| 6 | 2 | 2,112 | 384 | 4,608 | 36,864 | 8419 Hz |

## File Structure

**Created:**
- `src/MatrixHub75BitPlane.h` — the bit-plane extraction arithmetic, as `constexpr` functions with no Arduino or platform dependencies. Sole responsibility: given a source channel width, a plane count, and a plane index, produce the bit mask that selects that plane's source bit. Dependency-free specifically so the host test can include it directly.
- `extras/test/host/test_bitplane.cpp` — host unit tests for the above.
- `extras/test/host/run.ps1` — compiles and runs the host tests.
- `extras/test/compile-check/platformio.ini` — ESP32 compile verification at a parameterised depth.
- `extras/test/compile-check/src/main.cpp` — minimal sketch matching the target hardware config.
- `extras/test/README.md` — how to run both test layers.
- `docs/superpowers/results/2026-08-24-low-color-depth-measurements.md` — recorded hardware measurements (Task 7).

**Modified:**
- `src/MatrixCommonHub75.h:24-27` — add the include for the new header.
- `src/MatrixEsp32Hub75Calc_Impl.h:894-901` — `loadMatrixBuffers24` ladder to helper call.
- `src/MatrixEsp32Hub75Calc_Impl.h:571-578` — `loadMatrixBuffers48` ladder to helper call.
- `src/MatrixEsp32Hub75Calc_Impl.h:1116-1121` — depth dispatch.
- `src/MatrixEsp32Hub75Calc.h:31-32` — compile-time guard.

`extras/` is chosen for tests because the Arduino library specification tells the IDE to ignore that folder, so adding tests there cannot affect anyone compiling the library normally.

## ⚠️ The single most dangerous thing in this plan

`loadMatrixBuffers24` and `loadMatrixBuffers48` contain **byte-for-byte identical** ladder text:

```cpp
            int maskoffset = 0;
            if(COLOR_DEPTH_BITS == 12)   // 36-bit color
                maskoffset = 4;
            else if (COLOR_DEPTH_BITS == 16) // 48-bit color
                maskoffset = 0;
            else if (COLOR_DEPTH_BITS == 8)  // 24-bit color
                maskoffset = 0;

            uint16_t mask = (1 << (j + maskoffset));
```

They require **different** replacements, because `loadMatrixBuffers24` reads `rgb24` temp rows (8-bit channels) and `loadMatrixBuffers48` reads `rgb48` temp rows (16-bit channels). A global find-and-replace, or any `sed s///g`, will silently corrupt the 36/48-bit path.

**Always disambiguate by line number or by including surrounding context in the match.** Task 3 edits the *later* occurrence (around line 894, inside `loadMatrixBuffers24`); Task 4 edits the *earlier* one (around line 571, inside `loadMatrixBuffers48`). Task 3 runs first specifically so that by the time Task 4 runs, only one ladder remains and the match is unambiguous.

---

### Task 1: Host-testable bit-plane arithmetic

Creates the pure-arithmetic header and its host tests. Nothing in the library uses it yet — this task only establishes the tested building block and the test harness that later tasks depend on.

**Files:**
- Create: `src/MatrixHub75BitPlane.h`
- Create: `extras/test/host/test_bitplane.cpp`
- Create: `extras/test/host/run.ps1`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `constexpr int hub75MaskOffset(int sourceChannelBits, int numPlanes)` — returns the bit position within a source channel that feeds plane 0.
  - `constexpr unsigned int hub75PlaneMask(int sourceChannelBits, int numPlanes, int plane)` — returns the single-bit mask selecting `plane`'s source bit.
  - `#define HUB75_SOURCE_BITS_RGB24 8`
  - `#define HUB75_SOURCE_BITS_RGB48 16`

- [ ] **Step 1: Write the failing test**

Create `extras/test/host/test_bitplane.cpp`:

```cpp
// Host unit tests for HUB75 bit-plane extraction arithmetic.
// Build and run with extras/test/host/run.ps1 — no hardware required.
#include "MatrixHub75BitPlane.h"
#include <cstdio>

static int failures = 0;
#define CHECK(expr) do { if(!(expr)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while(0)

// The ladder that MatrixEsp32Hub75Calc_Impl.h used before this change.
// Kept here so the new formula is proven equivalent for the depths that shipped.
static int legacyMaskOffset(int numPlanes) {
    if (numPlanes == 12) return 4;
    else if (numPlanes == 16) return 0;
    else if (numPlanes == 8) return 0;
    return 0;
}

int main() {
    // Equivalence with the shipped ladder, for every depth that previously worked.
    CHECK(hub75MaskOffset(HUB75_SOURCE_BITS_RGB24, 8) == legacyMaskOffset(8));
    CHECK(hub75MaskOffset(HUB75_SOURCE_BITS_RGB48, 12) == legacyMaskOffset(12));
    CHECK(hub75MaskOffset(HUB75_SOURCE_BITS_RGB48, 16) == legacyMaskOffset(16));

    // Masks for the shipped depths must be exactly what the old code produced.
    for (int j = 0; j < 8; ++j)
        CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, 8, j) == (1u << j));
    for (int j = 0; j < 12; ++j)
        CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB48, 12, j) == (1u << (j + 4)));
    for (int j = 0; j < 16; ++j)
        CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB48, 16, j) == (1u << j));

    // A saturated channel must light every plane at every supported depth.
    // This is what makes the target UI's five colours depth-independent.
    for (int n = 2; n <= 8; ++n)
        for (int j = 0; j < n; ++j)
            CHECK((0xFFu & hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, n, j)) != 0);

    // A zero channel must light no plane at any depth.
    for (int n = 2; n <= 8; ++n)
        for (int j = 0; j < n; ++j)
            CHECK((0x00u & hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, n, j)) == 0);

    // Reduced depth must take the TOP bits of the source channel, not the bottom.
    for (int j = 0; j < 4; ++j)
        CHECK((0xF0u & hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, 4, j)) != 0);
    for (int j = 0; j < 4; ++j)
        CHECK((0x0Fu & hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, 4, j)) == 0);

    // Plane 0 is the LSB of the used range; plane n-1 is the MSB of the channel.
    CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, 4, 0) == 0x10u);
    CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, 4, 3) == 0x80u);
    CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, 2, 0) == 0x40u);
    CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, 2, 1) == 0x80u);

    if (failures == 0) std::printf("ALL TESTS PASSED\n");
    else std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
```

Create `extras/test/host/run.ps1`:

```powershell
# Builds and runs the host-side SmartMatrix unit tests.
# Requires clang++ (any C++17 compiler works; adjust $clang if yours differs).
$ErrorActionPreference = "Stop"
$clang = "C:\Program Files\LLVM\bin\clang++.exe"
$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$src   = Join-Path $here "..\..\..\src"
$out   = Join-Path $here "test_bitplane.exe"

& $clang -std=c++17 -Wall -Wextra -I $src (Join-Path $here "test_bitplane.cpp") -o $out
if ($LASTEXITCODE -ne 0) { Write-Error "compile failed"; exit 1 }
& $out
exit $LASTEXITCODE
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```powershell
powershell -File extras\test\host\run.ps1
```

Expected: compile failure, `fatal error: 'MatrixHub75BitPlane.h' file not found`. The header does not exist yet.

- [ ] **Step 3: Write the minimal implementation**

Create `src/MatrixHub75BitPlane.h`:

```cpp
/*
 * SmartMatrix Library - HUB75 bit-plane extraction arithmetic
 *
 * Pure arithmetic with no Arduino, platform, or template dependencies, so it
 * can be unit-tested on the host. See extras/test/host/.
 */

#ifndef MatrixHub75BitPlane_h
#define MatrixHub75BitPlane_h

// Width in bits of one colour channel in the layer data feeding the refresh
// buffer. loadMatrixBuffers24() reads rgb24 temp rows, loadMatrixBuffers48()
// reads rgb48 temp rows.
#define HUB75_SOURCE_BITS_RGB24 8
#define HUB75_SOURCE_BITS_RGB48 16

// Bit position within a source colour channel that feeds bit-plane 0.
//
// Bit-planes are numbered 0 = LSB through numPlanes-1 = MSB, and the TOP
// numPlanes bits of the source channel are used. Reducing numPlanes therefore
// discards the least significant source bits, which is what makes a saturated
// channel (all ones) render identically at every depth.
constexpr int hub75MaskOffset(int sourceChannelBits, int numPlanes) {
    return sourceChannelBits - numPlanes;
}

// Single-bit mask selecting the source bit that feeds the given bit-plane.
constexpr unsigned int hub75PlaneMask(int sourceChannelBits, int numPlanes, int plane) {
    return 1u << (plane + hub75MaskOffset(sourceChannelBits, numPlanes));
}

#endif
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```powershell
powershell -File extras\test\host\run.ps1
```

Expected: `ALL TESTS PASSED`, exit code 0.

- [ ] **Step 5: Verify the test can actually fail**

A test that cannot fail is not a test. Temporarily change `hub75MaskOffset` to `return 0;`, re-run, and confirm you see `25 FAILURE(S)` and exit code 1. Then revert the change and re-run to confirm `ALL TESTS PASSED` again.

- [ ] **Step 6: Commit**

```bash
git add src/MatrixHub75BitPlane.h extras/test/host/test_bitplane.cpp extras/test/host/run.ps1
git commit -m "test: add host-testable HUB75 bit-plane arithmetic

Extracts the bit-plane mask calculation into a dependency-free header so
it can be unit-tested without hardware. Not yet used by the library."
```

---

### Task 2: ESP32 compile-check harness

Builds a minimal PlatformIO project matching the target hardware, parameterised by refresh depth. Tasks 3 through 6 all use it to confirm the library still compiles. This is also what demonstrates the bug: depth 18 currently compiles cleanly and would render garbage.

**Files:**
- Create: `extras/test/compile-check/platformio.ini`
- Create: `extras/test/compile-check/src/main.cpp`
- Create: `extras/test/README.md`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: a `pio run` build parameterised by the `REFRESH_DEPTH` build flag, used verbatim by Tasks 3-6.

- [ ] **Step 1: Create the compile-check project**

Create `extras/test/compile-check/platformio.ini`. Note the platform pin — it is required, see Global Constraints:

```ini
; Compile-only verification that the library builds for the target hardware
; at a given refresh depth. Does not need a board attached.
;
; One env per depth, selected with -e:
;   pio run -e depth12
;
; PlatformIO 6.1.19 has no --build-flag option, so the depth cannot be passed
; on the command line. Envs keep it reproducible without shell state.
;
; platform MUST stay pinned. Unpinned "espressif32" resolves to a dev snapshot
; that crashes before compiling.
[env]
platform = espressif32@6.9.0
board = esp32dev
framework = arduino
lib_deps =
    symlink://../../..
    adafruit/Adafruit GFX Library@^1.11.9
    adafruit/Adafruit BusIO@^1.16.1
build_flags = -DGPIOPINOUT=7

; --- depths that must build ---
[env:depth6]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=6
[env:depth9]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=9
[env:depth12]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=12
[env:depth18]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=18
[env:depth24]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=24
[env:depth36]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=36
[env:depth48]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=48

; --- depths that must FAIL once Task 6 adds the guard ---
[env:bad20]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=20
[env:bad3]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=3
[env:bad51]
build_flags = ${env.build_flags} -DREFRESH_DEPTH=51
```

Note the bare `pio run` with no `-e` builds **every** env, including the three
that are meant to fail after Task 6. Always pass `-e`.

Create `extras/test/compile-check/src/main.cpp`, mirroring the target hardware config:

```cpp
// Minimal sketch matching the target hardware, used for compile verification.
// GPIOPINOUT 7 is HUB75_ADAPTER_V0_SMT_PINOUT: 8-bit I2S with an external
// ADDX latch, CLKS_DURING_LATCH 4.
#define USE_ADAFRUIT_GFX_LAYERS
#include <MatrixHardware_ESP32_V0.h>
#include <SmartMatrix.h>

#define COLOR_DEPTH 24

const uint16_t kMatrixWidth  = 64;
const uint16_t kMatrixHeight = 32;
const uint8_t  kRefreshDepth = REFRESH_DEPTH;
const uint8_t  kDmaBufferRows = 4;
const uint8_t  kPanelType = SMARTMATRIX_HUB75_32ROW_64COL_MOD8SCAN;
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);
const uint8_t  kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);

SMARTMATRIX_ALLOCATE_BUFFERS(matrixLayer, kMatrixWidth, kMatrixHeight,
    kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth,
    kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);

void setup() {
    matrixLayer.addLayer(&backgroundLayer);
    matrixLayer.begin();
}

void loop() {
    backgroundLayer.fillScreen({255, 0, 0});
    backgroundLayer.swapBuffers();
}
```

- [ ] **Step 2: Verify the harness builds at the current supported depth**

Run:
```powershell
cd extras\test\compile-check
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth24
```

Expected: `[SUCCESS]`, roughly 20-30 seconds on a cold build. If you instead see `TypeError: argument should be a str or an os.PathLike object`, the platform pin is missing or wrong.

- [ ] **Step 3: Demonstrate the bug this plan fixes**

Run:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth18
```

Expected: `[SUCCESS]`. **This is the bug.** Depth 18 compiles without complaint today, but the dispatch at `MatrixEsp32Hub75Calc_Impl.h:1116-1121` matches no branch, so `loadMatrixBuffers*` is never called, the frame buffer is never written, and the panel shows garbage. Record that this succeeded — Task 6 turns it into a build error, and Task 5 makes it actually work.

- [ ] **Step 4: Document how to run the tests**

Create `extras/test/README.md`:

```markdown
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
```

- [ ] **Step 5: Commit**

```bash
git add extras/test/compile-check extras/test/README.md
git commit -m "test: add ESP32 compile-check harness

Minimal PlatformIO project matching the target hardware, parameterised by
REFRESH_DEPTH. Confirms depth 18 currently compiles silently, which is the
bug the following commits fix."
```

---

### Task 3: Use the helper in `loadMatrixBuffers24`

Replaces the 8-bit-source ladder with a call to the tested helper. Behaviour at depth 24 must be identical: the old ladder yields `maskoffset = 0`, and `hub75MaskOffset(8, 8)` is also 0.

**Files:**
- Modify: `src/MatrixCommonHub75.h:24-27`
- Modify: `src/MatrixEsp32Hub75Calc_Impl.h:894-901`

**Interfaces:**
- Consumes: `hub75PlaneMask(int, int, int)` and `HUB75_SOURCE_BITS_RGB24` from Task 1.
- Produces: nothing new.

- [ ] **Step 1: Make the helper visible to the library**

In `src/MatrixCommonHub75.h`, immediately after the include guard at line 25, add the include. The file currently reads:

```cpp
#ifndef SmartMatrixCommonHUB75_h
#define SmartMatrixCommonHUB75_h

#define DEFAULT_PANEL_WIDTH_FOR_LINEAR_PANELS       32
```

Change it to:

```cpp
#ifndef SmartMatrixCommonHUB75_h
#define SmartMatrixCommonHUB75_h

#include "MatrixHub75BitPlane.h"

#define DEFAULT_PANEL_WIDTH_FOR_LINEAR_PANELS       32
```

- [ ] **Step 2: Replace the ladder in `loadMatrixBuffers24`**

⚠️ There are **two identical copies** of this text in the file. You want the one at approximately **line 894**, inside `loadMatrixBuffers24`. Confirm before editing:

```bash
grep -n "loadMatrixBuffers24\|loadMatrixBuffers48" src/MatrixEsp32Hub75Calc_Impl.h
```

The correct occurrence is the one whose enclosing function is `loadMatrixBuffers24` (the later of the two ladders). Replace this block:

```cpp
        for(int j=0; j<COLOR_DEPTH_BITS; j++) {
            int maskoffset = 0;
            if(COLOR_DEPTH_BITS == 12)   // 36-bit color
                maskoffset = 4;
            else if (COLOR_DEPTH_BITS == 16) // 48-bit color
                maskoffset = 0;
            else if (COLOR_DEPTH_BITS == 8)  // 24-bit color
                maskoffset = 0;

            uint16_t mask = (1 << (j + maskoffset));
```

with:

```cpp
        for(int j=0; j<COLOR_DEPTH_BITS; j++) {
            // tempRow0/tempRow1 are rgb24 here, so the source channel is 8 bits.
            // The top COLOR_DEPTH_BITS of each channel feed the bit-planes.
            uint16_t mask = hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, COLOR_DEPTH_BITS, j);
```

- [ ] **Step 3: Verify the host tests still pass**

Run:
```powershell
powershell -File extras\test\host\run.ps1
```

Expected: `ALL TESTS PASSED`. These already assert `hub75PlaneMask(8, 8, j) == (1u << j)`, which is precisely the equivalence this edit relies on.

- [ ] **Step 4: Verify it still compiles at depth 24**

Run:
```powershell
cd extras\test\compile-check
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth24
```

Expected: `[SUCCESS]`.

- [ ] **Step 5: Commit**

```bash
git add src/MatrixCommonHub75.h src/MatrixEsp32Hub75Calc_Impl.h
git commit -m "refactor: use tested bit-plane helper in loadMatrixBuffers24

Replaces the hardcoded maskoffset ladder with hub75PlaneMask(). Behaviour
at depth 24 is unchanged: the ladder yielded 0 and so does the helper."
```

---

### Task 4: Use the helper in `loadMatrixBuffers48`

Same replacement, but the source channels are 16-bit. The old ladder yields 4 for 12 planes and 0 for 16 planes; `hub75MaskOffset(16, 12)` is 4 and `hub75MaskOffset(16, 16)` is 0.

**Files:**
- Modify: `src/MatrixEsp32Hub75Calc_Impl.h:571-578`

**Interfaces:**
- Consumes: `hub75PlaneMask(int, int, int)` and `HUB75_SOURCE_BITS_RGB48` from Task 1.
- Produces: nothing new.

- [ ] **Step 1: Replace the remaining ladder**

After Task 3 there is exactly one ladder left in the file, inside `loadMatrixBuffers48` at approximately line 571. Confirm:

```bash
grep -n "maskoffset" src/MatrixEsp32Hub75Calc_Impl.h
```

Expected: matches only within `loadMatrixBuffers48`. If you see matches in two different functions, Task 3 was not completed — stop and finish it first.

Replace this block:

```cpp
        for(int j=0; j<COLOR_DEPTH_BITS; j++) {
            int maskoffset = 0;
            if(COLOR_DEPTH_BITS == 12)   // 36-bit color
                maskoffset = 4;
            else if (COLOR_DEPTH_BITS == 16) // 48-bit color
                maskoffset = 0;
            else if (COLOR_DEPTH_BITS == 8)  // 24-bit color
                maskoffset = 0;

            uint16_t mask = (1 << (j + maskoffset));
```

with:

```cpp
        for(int j=0; j<COLOR_DEPTH_BITS; j++) {
            // tempRow0/tempRow1 are rgb48 here, so the source channel is 16 bits.
            // The top COLOR_DEPTH_BITS of each channel feed the bit-planes.
            uint16_t mask = hub75PlaneMask(HUB75_SOURCE_BITS_RGB48, COLOR_DEPTH_BITS, j);
```

- [ ] **Step 2: Verify no ladder remains**

Run:
```bash
grep -n "maskoffset" src/MatrixEsp32Hub75Calc_Impl.h
```

Expected: no output. Every occurrence is now inside `MatrixHub75BitPlane.h`.

- [ ] **Step 3: Verify the host tests still pass**

Run:
```powershell
powershell -File extras\test\host\run.ps1
```

Expected: `ALL TESTS PASSED`. The tests assert `hub75PlaneMask(16, 12, j) == (1u << (j + 4))` and `hub75PlaneMask(16, 16, j) == (1u << j)`, covering both depths this function handles.

- [ ] **Step 4: Verify it compiles at 24, 36 and 48**

Run all three:
```powershell
cd extras\test\compile-check
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth24
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth36
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth48
```

Expected: `[SUCCESS]` for all three. Depth 36 and 48 exercise `loadMatrixBuffers48`, which this task just changed.

- [ ] **Step 5: Commit**

```bash
git add src/MatrixEsp32Hub75Calc_Impl.h
git commit -m "refactor: use tested bit-plane helper in loadMatrixBuffers48

The last hardcoded maskoffset ladder is gone. Behaviour at depths 36 and
48 is unchanged: 16-12=4 and 16-16=0 match the old ladder exactly."
```

---

### Task 5: Widen the depth dispatch

The dispatch matches exact plane counts, so any depth below 24 calls nothing and leaves the frame buffer unwritten. Widening the 8-plane case to `<= 8` routes low depths to `loadMatrixBuffers24`, which reads rgb24 temp rows — already what `MatrixEsp32Hub75Calc_Impl.h:379` allocates for anything under 12 planes.

**Files:**
- Modify: `src/MatrixEsp32Hub75Calc_Impl.h:1116-1121`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: working rendering at plane counts 2 through 8.

- [ ] **Step 1: Widen the dispatch**

At approximately line 1116, replace:

```cpp
        // TODO: support rgb36/48 with same function, copy function to rgb24
        if(COLOR_DEPTH_BITS == 16)
            loadMatrixBuffers48(currentFrameDataPtr, currentRow, lsbMsbTransitionBit, numBrightnessShifts);
        else if(COLOR_DEPTH_BITS == 12)
            loadMatrixBuffers48(currentFrameDataPtr, currentRow, lsbMsbTransitionBit, numBrightnessShifts);
        else if(COLOR_DEPTH_BITS == 8)
            loadMatrixBuffers24(currentFrameDataPtr, currentRow, lsbMsbTransitionBit, numBrightnessShifts);
```

with:

```cpp
        // rgb48 source for 12 and 16 planes, rgb24 source for 8 and below.
        // This must match the tempRow allocation in begin(), which uses rgb48
        // for 12 or 16 planes and rgb24 otherwise.
        if(COLOR_DEPTH_BITS == 16)
            loadMatrixBuffers48(currentFrameDataPtr, currentRow, lsbMsbTransitionBit, numBrightnessShifts);
        else if(COLOR_DEPTH_BITS == 12)
            loadMatrixBuffers48(currentFrameDataPtr, currentRow, lsbMsbTransitionBit, numBrightnessShifts);
        else if(COLOR_DEPTH_BITS <= 8)
            loadMatrixBuffers24(currentFrameDataPtr, currentRow, lsbMsbTransitionBit, numBrightnessShifts);
```

- [ ] **Step 2: Confirm the tempRow allocation already agrees**

Read `src/MatrixEsp32Hub75Calc_Impl.h:379-385`. It should read:

```cpp
    if((COLOR_DEPTH_BITS == 12) || (COLOR_DEPTH_BITS == 16)){
        tempRow0Ptr = malloc(sizeof(rgb48) * numPixelsPerTempRow);
        tempRow1Ptr = malloc(sizeof(rgb48) * numPixelsPerTempRow);
    } else {
        tempRow0Ptr = malloc(sizeof(rgb24) * numPixelsPerTempRow);
        tempRow1Ptr = malloc(sizeof(rgb24) * numPixelsPerTempRow);
    }
```

The `else` branch already allocates rgb24 for any depth below 12, which is what `loadMatrixBuffers24` expects. **No change needed here** — this step is a read-only confirmation that the two sites agree. If they do not, stop: routing rgb48 buffers into `loadMatrixBuffers24` would read past the end of each pixel.

- [ ] **Step 3: Verify it compiles at every depth to be supported**

Run:
```powershell
cd extras\test\compile-check
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth24
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth18
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth12
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth9
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e depth6
```

Expected: `[SUCCESS]` for all five.

- [ ] **Step 4: Commit**

```bash
git add src/MatrixEsp32Hub75Calc_Impl.h
git commit -m "feat: dispatch refresh depths below 24 to loadMatrixBuffers24

The dispatch matched exact plane counts, so any depth under 24 called
nothing and left the frame buffer unwritten. Widening the 8-plane case to
<= 8 routes 2 through 8 planes to the rgb24 path, matching what begin()
already allocates for temp rows."
```

---

### Task 6: Compile-time guard for invalid depths

With Task 5 done, plane counts 2-8 work. Everything else must now be a build error rather than a silently wrong display: a depth that is not a multiple of 3 truncates unexpectedly, a single plane cannot drive row addressing on direct-GPIO pinouts, and more than 16 planes exceeds the 16-bit rgb48 source.

**Files:**
- Modify: `src/MatrixEsp32Hub75Calc.h:31-32`

**Interfaces:**
- Consumes: `COLOR_DEPTH_BITS` and `COLOR_CHANNELS_PER_PIXEL` from `MatrixCommonHub75.h`, which `SmartMatrix.h` includes before this header.
- Produces: build-time rejection of invalid `refreshDepth`.

- [ ] **Step 1: Confirm the guard is currently absent**

Run:
```powershell
cd extras\test\compile-check
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e bad20
```

Expected: `[SUCCESS]`. Depth 20 is not a multiple of 3 — `COLOR_DEPTH_BITS` truncates to 6, so the panel would silently run at 18-bit while the sketch believes it asked for 20. This is what the guard prevents.

- [ ] **Step 2: Add the guard**

In `src/MatrixEsp32Hub75Calc.h`, the class currently opens:

```cpp
template <int refreshDepth, int matrixWidth, int matrixHeight, unsigned char panelType, uint32_t optionFlags>
class SmartMatrixHub75Calc {
public:
    typedef typename SmartMatrixHub75Refresh<refreshDepth, matrixWidth, matrixHeight, panelType, optionFlags>::frameStruct frameStruct;
```

Insert the asserts immediately after `public:`:

```cpp
template <int refreshDepth, int matrixWidth, int matrixHeight, unsigned char panelType, uint32_t optionFlags>
class SmartMatrixHub75Calc {
public:
    static_assert(refreshDepth % COLOR_CHANNELS_PER_PIXEL == 0,
        "SmartMatrix: kRefreshDepth must be a multiple of 3 (one bit-plane count per colour channel). Valid values: 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.");
    static_assert(COLOR_DEPTH_BITS >= 2,
        "SmartMatrix: kRefreshDepth must be at least 6. A single bit-plane cannot drive row addressing on direct-GPIO pinouts, where plane 0 carries the previous row's address.");
    static_assert(COLOR_DEPTH_BITS <= 16,
        "SmartMatrix: kRefreshDepth must be at most 48. The rgb48 source has only 16 bits per colour channel.");

    typedef typename SmartMatrixHub75Refresh<refreshDepth, matrixWidth, matrixHeight, panelType, optionFlags>::frameStruct frameStruct;
```

- [ ] **Step 3: Verify invalid depths are now rejected**

Run each and confirm the build **fails** with the matching message:

```powershell
cd extras\test\compile-check
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e bad20
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e bad3
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e bad51
```

Expected:
- depth 20: `[FAILED]`, `kRefreshDepth must be a multiple of 3`
- depth 3: `[FAILED]`, `kRefreshDepth must be at least 6`
- depth 51: `[FAILED]`, `kRefreshDepth must be at most 48`

- [ ] **Step 4: Verify every valid depth still builds**

Run:
```powershell
cd extras\test\compile-check
foreach ($e in "depth6","depth9","depth12","depth18","depth24","depth36","depth48") {
    Write-Host "=== $e ==="
    & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e $e | Select-String "SUCCESS|FAILED"
}
```

Expected: `[SUCCESS]` for all seven.

- [ ] **Step 5: Commit**

```bash
git add src/MatrixEsp32Hub75Calc.h
git commit -m "feat: reject invalid kRefreshDepth at compile time

A depth that is not a multiple of 3 silently truncates, a single plane
cannot drive row addressing, and more than 16 planes exceeds the rgb48
source. All three are now build errors naming the valid range."
```

---

### Task 7: Hardware measurement and acceptance

Everything so far is verified by compilation and host arithmetic. This task confirms the change does what it was for, on the panel, and records the numbers.

**Files:**
- Create: `docs/superpowers/results/2026-08-24-low-color-depth-measurements.md`

**Interfaces:**
- Consumes: the working build from Tasks 3-6.
- Produces: measured confirmation of the spec's acceptance criteria.

- [ ] **Step 1: Re-establish the baseline**

Flash the real application sketch unchanged at `kRefreshDepth = 24`. Capture the full serial boot log.

Confirm it matches the pre-change baseline exactly:

| Log line | Expected |
| --- | --- |
| `sizeof framestruct:` | `00002100` |
| `Descriptors for lsbMsbTransitionBit 0/7 ... require` | `24576 bytes` |
| `lsbMsbTransitionBit of 0 gives` | `99 Hz refresh` |

**This is the regression gate.** If any line differs, the refactor changed behaviour it should not have — stop and investigate before going further. Also look at the panel: the UI must be visually identical to before.

- [ ] **Step 2: Repeat the baseline for the other panel type**

Rebuild with `kPanelType = SMARTMATRIX_HUB75_32ROW_MOD16SCAN` at depth 24, flash, and capture the log. Record `sizeof framestruct`, descriptor bytes, and refresh rate. This panel has `MATRIX_SCAN_MOD` 16 and `PIXELS_PER_LATCH` 64 against the other's 8 and 128, but the same total pixel count, so `sizeof framestruct` should again be `00002100`. Confirm the display is correct on the hardware that uses this panel.

- [ ] **Step 3: Measure each reduced depth**

For each of `kRefreshDepth` = 18, 12, 9, 6, on both panel types: build, flash, capture the boot log, and photograph the panel showing each of the five UI colours plus white.

Record for each: `sizeof framestruct`, descriptor bytes, `lsbMsbTransitionBit`, refresh rate, and the `show_esp32_heap_mem()` totals before and after SmartMatrix allocation.

Compare against the projections in the table at the top of this plan. `sizeof framestruct` and descriptor bytes should match exactly — they are pure arithmetic. Refresh rate should match within rounding.

- [ ] **Step 4: Judge visual quality**

At each depth, with the panel in its normal viewing position, check:
- all five UI colours (red, green, blue, yellow, purple) — expected identical at every depth, since each channel is `0x00` or `0xFF`
- white — the only colour lighting all three channels at once, and the most likely to reveal a problem
- any dimmed, faded, or anti-aliased element the UI has acquired since the spec was written

Note the lowest depth that is visually acceptable.

- [ ] **Step 5: Watch for the high-refresh-rate risk**

At depth 12 and below the refresh rate exceeds 1.6 kHz, far outside this library's normal operating range. Look specifically for ghosting, dimming, flicker, or row bleed — symptoms of the panel's driver chips being pushed past their limits.

If they appear, the depth reduction is still available purely as a RAM win: raise `minRefreshRate` via `matrixLayer.setRefreshRate()`, or lower `ESP32_I2S_CLOCK_SPEED`, and re-measure.

- [ ] **Step 6: Record the results**

Create `docs/superpowers/results/2026-08-24-low-color-depth-measurements.md` with a table of every depth and panel type measured: projected versus actual for `sizeof framestruct`, descriptor bytes, refresh rate, and free heap; the visual verdict at each depth; and the chosen production depth with the reasoning.

State explicitly whether the spec's acceptance criterion was met: **`kRefreshDepth` 12 renders the UI indistinguishably from 24 and frees at least 30 KB of DMA-capable RAM.**

If it was not met, say which part failed and what the fallback depth is.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/results/2026-08-24-low-color-depth-measurements.md
git commit -m "docs: record low-colour-depth hardware measurements"
```

---

## Follow-on work (deliberately not in this plan)

Recorded so it is not lost, and kept out so the measurement above stays clean:

- **`rgb16` background layer**, worth about 4 KB. Blocked on a real bug: `Layer_BackgroundGfx_Impl.h:142-146` takes the `sizeof(RGB) <= 3` branch and indexes a 256-entry LUT with `rgb16`'s 5-bit `red` field, so only the bottom 12% of the LUT is ever reached and the image renders far too dark. The `rgb16(uint8_t, uint8_t, uint8_t)` constructor truncates rather than scales, so `rgb16(128, 0, 0)` gives red 0. Both need fixing before `rgb16` is usable.
- **Single-buffering the background layer**, halving its allocation. Needs analysis of whether the sketch fully redraws each frame or draws on top of the previous one.
- **`if constexpr` for the depth dispatch**, a flash and compile-time win rather than a RAM win.
- **Stripping the library to one hardware configuration.** Worth doing for maintainability, but it saves no measurable RAM or flash — the other platforms are already `#if`-excluded. The measurements from Task 7 become the regression baseline for it.
- **The `_NT` implementation** still supports only 24/36/48 and silently misrenders anything else, exactly as it does today. If it is ever adopted, it needs a runtime check in `begin()` since a `static_assert` cannot see a runtime depth.
