#pragma once

#include "Image.h"

// Controls image brightness.
//
// For every colour channel:   out = clamp(in * factor + offset, 0, 255)
//
//   factor  1.0 = unchanged, 0.0 = black, 2.0 = twice as bright, up to 3.0
//   offset  0 = unchanged, positive brightens, negative darkens (-255..255)
//
// The alpha (transparency) channel is never modified.
class BrightnessController {
public:
    static constexpr float kMinFactor = 0.0f;
    static constexpr float kMaxFactor = 3.0f;
    static constexpr int kMinOffset = -255;
    static constexpr int kMaxOffset = 255;

    float factor() const { return factor_; }
    int offset() const { return offset_; }

    // Values outside the allowed range are clamped. Returns false if the
    // value had to be clamped (i.e. it was out of range).
    bool setFactor(float factor);
    bool setOffset(int offset);

    void reset();

    Image apply(const Image& source) const;

private:
    float factor_ = 1.0f;
    int offset_ = 0;
};
