# ESP32 HUB75: Sub-24-bit Refresh Depth

**Date:** 2026-08-24
**Status:** Approved for planning
**Goal:** Free DMA-capable RAM on ESP32 by supporting refresh depths below 24-bit, without removing or restructuring anything else.

## Context

The SmartMatrix ESP32 HUB75 path hardcodes support for exactly three refresh
depths: 24, 36, and 48 bits (8, 12, and 16 bit-planes). The application driving
this work is a fixed-colour UI — large numerals in user-selectable red, green,
blue, yellow, or purple — rendered through an Adafruit_GFX background layer.

Every one of those colours is fully saturated: each channel is `0x00` or `0xFF`.
The background gamma LUT maps 0 to 0 and 255 to 255, and masking the top N bits
of `0xFF` yields all-ones for any N. Adafruit_GFX fonts are monochrome bitmaps,
so the numerals have hard edges with no anti-aliasing to band.

**The UI therefore renders bit-identically from 24-bit down to 2 bits per
channel.** Reducing refresh depth is visually free for this application, and it
is the only configuration term that shrinks the DMA allocation.

## Measured baseline

Captured from the boot log of the target build:

| Quantity | Value |
| --- | --- |
| Panel | 64x32, `SMARTMATRIX_HUB75_32ROW_64COL_MOD8SCAN` |
| Pinout | 8-bit I2S with external ADDX latch (`CLKS_DURING_LATCH` 4, `MATRIX_DATA_STORAGE_TYPE` `uint8_t`) |
| `ESP32_I2S_CLOCK_SPEED` | 26.67 MHz |
| `MATRIX_SCAN_MOD` | 8 |
| `PIXELS_PER_LATCH` | 128 |
| `COLOR_DEPTH_BITS` | 8 (`kRefreshDepth` 24) |
| `lsbMsbTransitionBit` | **0** — all 8 planes are genuine bit-planes |
| Refresh rate | 99 Hz (70 Hz requested) |

DMA-capable RAM consumed by SmartMatrix, from the same log:

| Allocation | Bytes |
| --- | --- |
| Layers | 14,324 |
| Frame buffers (2 x `sizeof(frameStruct)` = 2 x 8,448) | 17,344 |
| DMA linked-list descriptors | 24,608 |
| **Total** | **56,276** |

Two observations drive the design:

1. **Descriptors cost more than the frame buffers.** At `lsbMsbTransitionBit` 0
   the descriptor count per row is `2^(COLOR_DEPTH_BITS - 1)` = 128, so
   descriptors scale *exponentially* with depth while frame buffers scale
   linearly. Reducing depth attacks the larger term harder.
2. **There is no approximation to reclaim.** `lsbMsbTransitionBit` is already 0,
   so the build is not trading colour fidelity for refresh rate today. Any depth
   reduction is a real reduction in levels per channel, not the removal of an
   existing fudge.

### Model validation

A model built from the formulas in `MatrixEsp32Hub75Refresh_Impl.h` and
`MatrixEsp32Hub75Refresh.h` reproduces all three logged values exactly:
`sizeof(frameStruct)` = 0x2100, descriptor RAM = 24,576, refresh = 99 Hz.
Projections below use that validated model.

Refresh rate has a clean closed form here. With `lsbMsbTransitionBit` at 0, the
per-frame latch count is `2^N - 1` for N planes, so refresh rate scales as
`99 Hz * 255 / (2^N - 1)`.

## Projected results

| `kRefreshDepth` | planes | levels/ch | Frames | Descriptors | DMA total | Saved | Refresh |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 24 (today) | 8 | 256 | 16,896 | 24,576 | 41,472 | -- | 99 Hz |
| 18 | 6 | 64 | 12,672 | 6,144 | 18,816 | **22,656** | 400 Hz |
| 12 | 4 | 16 | 8,448 | 1,536 | 9,984 | **31,488** | 1683 Hz |
| 9 | 3 | 8 | 6,336 | 768 | 7,104 | **34,368** | 3608 Hz |
| 6 | 2 | 4 | 4,224 | 384 | 4,608 | **36,864** | 8419 Hz |

At `kRefreshDepth` 12 the DMA footprint drops by 76%, freeing roughly 31 KB.

The calc task also gets proportionally cheaper: `loadMatrixBuffers24` runs its
per-pixel inner loop once per plane per row
(`MatrixEsp32Hub75Calc_Impl.h:894`), so 8 planes to 4 halves that work. The calc
task rate is decoupled from the refresh rate (`calc_refreshRate`), so a higher
refresh rate does not claw the saving back.

## Design

### Scope

Add support for `refreshDepth` values below 24 on the ESP32 HUB75 path.

**Nothing is removed and nothing is restructured.** Teensy 3, Teensy 4, APA102,
every pinout, every panel type, and every layer type stay exactly as they are.
Existing depths 24, 36, and 48 must remain byte-identical in behaviour.

### Change 1 — generalise `maskoffset`

`MatrixEsp32Hub75Calc_Impl.h:895-899` selects which source bits feed each
bit-plane using a hardcoded if-ladder over `COLOR_DEPTH_BITS` of 8, 12, and 16.

The ladder collapses to a single expression:

```
maskoffset = <bits in source channel> - COLOR_DEPTH_BITS
```

where the source channel is 8 bits for the `rgb24` path and 16 bits for the
`rgb48` path. This reproduces every existing case exactly (rgb24: 8-8 = 0;
rgb48: 16-12 = 4 and 16-16 = 0) and extends naturally to lower depths — at 4
planes it selects source bits 4 through 7, the top nibble.

Truncation, not rounding, is used to pick the top N bits. This matches existing
behaviour for 36-bit and is exact for saturated colours (`0xFF` maps to all-ones
at every N).

The same generalisation applies to the identical ladder in
`loadMatrixBuffers48` (`MatrixEsp32Hub75Calc_Impl.h:572-576`).

### Change 2 — widen the dispatch

`MatrixEsp32Hub75Calc_Impl.h:1116-1121` dispatches on exact equality
(`COLOR_DEPTH_BITS == 8`), so lower depths fall through and call nothing,
leaving the frame buffer filled with garbage. The `== 8` comparison becomes
`<= 8` so low depths route to `loadMatrixBuffers24`.

The tempRow allocation at `MatrixEsp32Hub75Calc_Impl.h:379` already falls
through to `rgb24` for anything below 12 planes and needs no change.

### Change 3 — compile-time guard

Add a `static_assert` rejecting a `refreshDepth` that is not a multiple of 3 or
that yields fewer than 2 planes, naming the valid set in the message. Without
it, a typo produces a silently wrong display rather than a build error.

### Why the refresh side needs no changes

`MatrixEsp32Hub75Refresh_Impl.h` is already fully generic in `COLOR_DEPTH_BITS`:
every loop is `for(i = ...; i < COLOR_DEPTH_BITS; i++)`, and the
`lsbMsbTransitionBit` auto-tuning for both the RAM and minimum-refresh-rate
constraints derives from `COLOR_DEPTH_BITS` directly. The DMA descriptor
construction at `MatrixEsp32Hub75Refresh_Impl.h:261-276` is likewise generic.

### Floor of 2 planes

The guard stops at 2 planes rather than 1.

On direct-GPIO pinouts (`CLKS_DURING_LATCH == 0`), plane `j == 0` deliberately
emits the *previous* row's address, because that row is still lit while the LSB
plane shifts out (`MatrixEsp32Hub75Calc_Impl.h:944-946`). With a single plane
every plane is `j == 0`, so the current row's address would never be output.

The target build uses a latch pinout, whose path sets
`gpioRowAddress = currentRow` unconditionally
(`MatrixEsp32Hub75Calc_Impl.h:1057`) and is not subject to this. A floor of 2 is
nonetheless chosen so the feature behaves identically across pinouts, and
because a single plane offers no per-channel brightness control and little
practical value.

## Non-goals

Deliberately excluded to keep the measurement clean. Each is a separate, smaller
win:

- **Stripping the library to one hardware configuration.** All unused platforms
  are already `#if`-excluded and contribute nothing to the binary or to RAM, so
  removing them saves nothing measurable. Worth doing for maintainability, but
  after this change, and with these measurements as the regression baseline.
- **Background layer storage depth (`rgb16` / `rgb8`).** Worth about 4 KB, but
  blocked on a real bug: `Layer_BackgroundGfx_Impl.h:142-146` takes the
  `sizeof(RGB) <= 3` branch and indexes a 256-entry LUT with a 5-bit field,
  so `rgb16` renders far too dark. The `rgb16(uint8_t, uint8_t, uint8_t)`
  constructor also truncates rather than scales. Both need fixing first.
- **Single-buffering the background layer.** Halves the layer allocation but
  risks tearing; needs its own analysis of the sketch's draw pattern.
- **`if constexpr` for the depth ladders.** A flash-size and compile-time win,
  not a RAM win.

## Verification

Both panel types in use — `SMARTMATRIX_HUB75_32ROW_MOD16SCAN` and
`SMARTMATRIX_HUB75_32ROW_64COL_MOD8SCAN` — at each depth.

**Regression gate (must pass before anything else counts):** at `kRefreshDepth`
24, 36, and 48 the boot log must be identical to the pre-change log, and the
display must be visually unchanged. The generalised `maskoffset` expression is
claimed to be arithmetically identical for these depths; this is what proves it.

**Per-depth measurement**, read from the log the library already prints:

- `sizeof framestruct` — compare against the projected Frames column (halved,
  as the log prints one buffer)
- `Descriptors for lsbMsbTransitionBit N/M ... require X bytes` — compare
  against the projected Descriptors column
- `lsbMsbTransitionBit of N gives X Hz refresh` — compare against the projected
  Refresh column
- `show_esp32_heap_mem()` before and after — confirms RAM actually returned to
  the application, which is the point of the exercise

**Visual check on hardware** at each depth: all five UI colours, plus white.
White is the case most likely to surprise, being the only one that lights all
three channels simultaneously.

**Acceptance:** `kRefreshDepth` 12 renders the UI indistinguishably from 24 and
frees at least 30 KB of DMA-capable RAM.

## Risks and open questions

- **Low-depth banding is real for non-saturated content.** Gamma is applied
  before quantisation, so the dim end is crushed hardest. If the UI later grows
  a fade, a gradient, or a dimmed state, the acceptable depth floor rises.
  18-bit is the conservative fallback and still frees 22 KB.
- **Very high refresh rates are untested territory.** 1683 Hz at 12-bit is far
  outside this library's normal operating range. Panel driver chips may ghost or
  misbehave. If so, the fix is available and cheap: raise `minRefreshRate`, or
  accept the depth reduction purely as a RAM win at a lower clock.
- **Descriptor count interacts with `lsbMsbTransitionBit` auto-tuning.** At low
  depths there is far more DMA headroom, so `lsbMsbTransitionBit` should stay at
  0. The logs will confirm rather than assume this.
- **The two panel types have different geometry** (`MATRIX_SCAN_MOD` 16 vs 8,
  `PIXELS_PER_LATCH` 64 vs 128) but identical total pixel counts, so both should
  scale identically. Measured, not assumed.
