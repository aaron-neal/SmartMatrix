# Low Colour Depth — Hardware Measurements

**Date:** 2026-08-24
**Branch:** `esp32-low-color-depth`
**Plan:** `docs/superpowers/plans/2026-08-24-esp32-low-color-depth.md`
**Spec:** `docs/superpowers/specs/2026-08-24-esp32-low-color-depth-design.md`

**Verdict: acceptance criterion MET.** `kRefreshDepth` 12 renders the UI
indistinguishably from 24 and frees 31,488 bytes (30.75 KB) of DMA-capable RAM.

## Hardware under test

64x32 panel, `SMARTMATRIX_HUB75_32ROW_64COL_MOD8SCAN`, `GPIOPINOUT` 7
(`HUB75_ADAPTER_V0_SMT_PINOUT` — 8-bit I2S with external ADDX latch,
`CLKS_DURING_LATCH` 4), `ESP32_I2S_CLOCK_SPEED` 26.67 MHz, classic ESP32.
Application: fixed-colour UI, large numerals in user-selectable red, green,
blue, yellow or purple, drawn on an Adafruit_GFX background layer.

## Regression gate — `kRefreshDepth` 24, before vs after the code change

The three values that had to be preserved were preserved exactly:

| Measurement | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `sizeof framestruct` | `0x2100` | `0x2100` | 0 |
| Descriptor RAM | 24,576 | 24,576 | 0 |
| `lsbMsbTransitionBit` | 0/7 | 0/7 | 0 |
| Refresh rate | 99 Hz | 99 Hz | 0 |

Every SmartMatrix allocation is byte-identical:

| Allocation | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Layers | 14,324 | 14,324 | 0 |
| Frame buffers | 17,344 | 17,344 | 0 |
| DMA descriptors | 24,608 | 24,608 | 0 |
| **Total** | **56,276** | **56,276** | **0** |

### On the 48-byte offset in the raw numbers

Every raw free-memory figure in the post-change log is 48 bytes lower on the
heap and 32 lower on DMA, and the frame buffer pointers moved by 16 bytes
(`3FFCB72C` to `3FFCB73C`).

This is not attributable to the library change. The offset is already present
on the first line of the log — `Starting SmartMatrix Mallocs`, before
SmartMatrix has allocated anything — so it originates in the sketch or
framework build that ran beforehand. Subtracting it out, every
SmartMatrix-attributable figure is identical, as the table above shows.

## `kRefreshDepth` 12 — measured against projection

Every projected value landed exactly:

| Measurement | Projected | Actual | Match |
| --- | ---: | ---: | :---: |
| `sizeof framestruct` | `0x1080` | `0x1080` | exact |
| Descriptor RAM | 1,536 | 1,536 | exact |
| `lsbMsbTransitionBit` | 0/3 | 0/3 | exact |
| Refresh rate | 1683 Hz | 1683 Hz | exact |
| Free heap at end | ~260,600 | 260,616 | +16 |

### RAM freed

| | depth 24 | depth 12 | freed |
| --- | ---: | ---: | ---: |
| Free heap at end | 229,128 | 260,616 | **31,488** |
| Free DMA at end | 186,788 | 218,276 | **31,488** |

Projected saving was 31,488 bytes. Measured saving is 31,488 bytes.

### Where the saving came from

| Allocation | depth 24 | depth 12 | freed |
| --- | ---: | ---: | ---: |
| Layers | 14,324 | 14,324 | 0 |
| Frame buffers | 17,344 | 8,896 | 8,448 |
| DMA descriptors | 24,608 | 1,568 | **23,040** |
| **Total** | **56,276** | **24,788** | **31,488** |

The descriptors supplied 23 KB of the 31.5 KB. This confirms the spec's central
finding: descriptor count is `2^(COLOR_DEPTH_BITS - 1)` per row, so it falls
exponentially with depth while the frame buffers only fall linearly. Layers are
untouched, correctly — layer storage does not depend on refresh depth.

`lsbMsbTransitionBit` came back 0/3, so all four bit-planes are genuine
bit-planes with no fractional-OE approximation.

## Visual result

The UI at `kRefreshDepth` 12 is visually indistinguishable from 24 by eye. This
is the expected outcome: all five UI colours are fully saturated, so every
channel is `0x00` or `0xFF` and renders bit-identically at any depth from 2
bit-planes upward.

**Unexpected improvement: banding visible through a phone camera at 24-bit is
gone at 12-bit.** This is not colour banding. It is rolling-shutter beating
against the panel refresh: at 99 Hz the camera's sensor scan captures different
parts of the BCM cycle on different rows, producing stripes. At 1683 Hz each
exposure row integrates roughly 17x more refresh cycles and the stripes average
out.

Two consequences worth recording:

1. The panel now photographs and films cleanly, which it did not before.
2. This is independent physical corroboration that the refresh rate genuinely
   increased. The boot log's 1683 Hz figure is a calculation; the camera
   confirms the same conclusion by an unrelated mechanism.

No ghosting, smearing, or brightness change was observed at 1683 Hz, so the
spec's "very high refresh rates are untested territory" risk did not
materialise on this panel.

## Not tested

Recorded so the coverage of this result is not overstated:

- **The second panel type.** Only `SMARTMATRIX_HUB75_32ROW_64COL_MOD8SCAN` was
  measured. `SMARTMATRIX_HUB75_32ROW_MOD16SCAN` (the other hardware version,
  `MATRIX_SCAN_MOD` 16, `PIXELS_PER_LATCH` 64) has not been run at any depth.
  Its geometry differs, so its multi-row refresh mapping is unexercised at
  reduced depth.
- **Depths other than 24 and 12.** 18, 9 and 6 are compile-verified only. Depth
  9 would free a further 2,880 bytes at 3608 Hz if wanted.
- **A grey ramp.** The visual check used the production UI, whose fully
  saturated colours render identically at every depth and therefore cannot
  reveal a bit-plane extraction error. A ramp of `rgb24(x*4, x*4, x*4)` across
  the width would show 2^planes discrete steps and is the test that would
  actually exercise the changed arithmetic on hardware. The host unit tests
  cover this arithmetic exhaustively for plane counts 2-16, so the risk is low,
  but it has not been confirmed on the panel.
- **Direct-GPIO (`CLKS_DURING_LATCH == 0`) pinouts.** The floor of 2 bit-planes
  exists because plane 0 carries the previous row's address on those pinouts.
  This build uses the latch path, so that boundary is untested on the pinout it
  was written for.
- **Scrolling and indexed layers at reduced depth.** Known broken — see the
  limitation recorded in `extras/test/README.md`. `SM_Layer::setRefreshRate()`
  takes a `uint8_t` while the calculated rate at depth 12 is 841, so it
  truncates to 73 and scroll speed is computed from a wrong figure. This
  application uses a background layer only and is unaffected.

## Conclusion

`kRefreshDepth` 12 is recommended for this application. It frees 30.75 KB of
DMA-capable RAM, is visually identical to 24-bit for fully saturated content,
and additionally removes camera banding as a side effect of the higher refresh
rate.
