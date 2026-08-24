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
