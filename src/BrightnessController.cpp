#include "BrightnessController.h"

#include <algorithm>

bool BrightnessController::setFactor(float factor) {
    const float clamped = std::clamp(factor, kMinFactor, kMaxFactor);
    const bool inRange = (clamped == factor);
    factor_ = clamped;
    return inRange;
}

bool BrightnessController::setOffset(int offset) {
    const int clamped = std::clamp(offset, kMinOffset, kMaxOffset);
    const bool inRange = (clamped == offset);
    offset_ = clamped;
    return inRange;
}

void BrightnessController::reset() {
    factor_ = 1.0f;
    offset_ = 0;
}

Image BrightnessController::apply(const Image& source) const {
    if (source.empty()) return Image();

    Image result = source;
    const int w = source.width();
    const int h = source.height();
    const int comp = source.channels();

    // Only colour channels are affected; alpha is preserved untouched.
    const int colorChannels = (comp == 2) ? 1 : std::min(comp, 3);
    unsigned char* pixels = result.pixels();
    const std::size_t pixelCount = static_cast<std::size_t>(w) * h;

    // Pre-compute a lookup table: 256 lookups instead of a multiply per pixel.
    unsigned char lut[256];
    for (int i = 0; i < 256; ++i) {
        const float v = static_cast<float>(i) * factor_ + static_cast<float>(offset_);
        lut[i] = static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f));
    }

    for (std::size_t p = 0; p < pixelCount; ++p) {
        unsigned char* pixel = pixels + p * comp;
        for (int c = 0; c < colorChannels; ++c) {
            pixel[c] = lut[pixel[c]];
        }
    }

    return result;
}
