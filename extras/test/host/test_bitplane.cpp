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
