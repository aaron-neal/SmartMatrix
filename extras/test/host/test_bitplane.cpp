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

    // Every plane count the dispatch admits must select the top n bits of the
    // 16-bit rgb48 source. n=9..11 and 13..15 are the counts the widened
    // dispatch newly enabled, so they are swept here rather than spot-checked.
    for (int n = 2; n <= 16; ++n)
        for (int j = 0; j < n; ++j)
            CHECK(hub75PlaneMask(HUB75_SOURCE_BITS_RGB48, n, j) == (1u << (j + 16 - n)));

    // Every mask must stay inside the source channel's 8-bit window. This is
    // weak on its own (it would not catch a wrong offset within the window),
    // but it would catch a mutation that shifted the mask outside the window.
    for (int n = 2; n <= 8; ++n)
        for (int j = 0; j < n; ++j)
            CHECK((0xFFu & hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, n, j)) != 0);

    // The masks for a given depth must be distinct and must together cover
    // exactly the top-n window of the source channel — this is what a wrong
    // offset actually breaks.
    for (int n = 2; n <= 8; ++n) {
        unsigned int combined = 0;
        for (int j = 0; j < n; ++j) combined |= hub75PlaneMask(HUB75_SOURCE_BITS_RGB24, n, j);
        CHECK(combined == ((0xFFu << (8 - n)) & 0xFFu));
    }

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
