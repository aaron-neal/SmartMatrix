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
