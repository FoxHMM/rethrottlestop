

#include "undervolt.hpp"

#include <cmath>

namespace uv {

uint32_t convertRoundedOffset(int x) {
    return static_cast<uint32_t>(0xFFE00000u) &
           (static_cast<uint32_t>(x & 0xFFF) << 21);
}

uint32_t convertOffset(double mV) {
    const int rounded = static_cast<int>(std::llround(mV * 1.024));
    return convertRoundedOffset(rounded);
}

int unconvertRoundedOffset(uint32_t y) {
    const uint32_t x = y >> 21;
    if (x <= 1024) {
        return static_cast<int>(x);
    }
    return -(2048 - static_cast<int>(x));
}

double unconvertOffset(uint32_t y) {
    return static_cast<double>(unconvertRoundedOffset(y)) / 1.024;
}

uint64_t packOffset(int planeIndex, uint32_t offset, bool write) {
    const uint64_t hasOffset = write ? 1ULL : 0ULL;
    return (1ULL << 63) | (static_cast<uint64_t>(planeIndex) << 40) |
           (1ULL << 36) | (hasOffset << 32) | static_cast<uint64_t>(offset);
}

double unpackOffset(uint64_t msrResponse) {
    const int planeIndex = static_cast<int>(msrResponse >> 40);
    const uint32_t value =
        static_cast<uint32_t>(msrResponse ^ (static_cast<uint64_t>(planeIndex) << 40));
    return unconvertOffset(value);
}

double toSeconds(uint64_t val, double unit) {
    const double exp = std::pow(2.0, static_cast<double>(val & 0x1F));
    const double mult = 1.0 + static_cast<double>((val >> 5) & 0x3) / 4.0;
    return exp * mult / unit;
}

uint64_t fromSeconds(double val, double unit) {
    const double product = val * unit;

    if (std::log2(product / 1.75) >= 31.0) {
        return 0xFE;
    }

    double minErr = 1e9;
    uint64_t result = 0;

    for (int y = 0; y < 4; ++y) {
        const double multiplier = 1.0 + static_cast<double>(y) / 4.0;
        const double valMult = product / multiplier;
        const double exp = std::log2(valMult);
        int expInt = static_cast<int>(exp);

        const double lower = std::pow(2.0, static_cast<double>(expInt));
        const double upper = std::pow(2.0, static_cast<double>(expInt + 1));
        if ((valMult - lower) >= (upper - valMult)) {
            expInt++;
        }
        if (expInt > 31) {
            expInt = 31;
        }

        const double backVal = std::pow(2.0, static_cast<double>(expInt)) * multiplier;
        const double errVal = std::fabs(backVal - product);
        if (errVal < minErr) {
            minErr = errVal;
            result = (static_cast<uint64_t>(y) << 5) | static_cast<uint64_t>(expInt);
        }
    }
    return result;
}

namespace {
constexpr int      kOffsetBase  = 2048;
constexpr int      kOffsetMask  = 0x7FF;
constexpr double   kOffsetScale = 1.024;
constexpr int      kRawShift    = 21;
constexpr int      kPlaneShift  = 40;
constexpr uint64_t kCmdBase     = 0x8000001000000000ULL;
constexpr uint64_t kWriteFlag   = 1ULL << 32;
}

int16_t encodeOffsetRaw(double mV) {
    const double v = static_cast<double>(kOffsetBase) - std::fabs(mV) * kOffsetScale;
    const int raw = static_cast<int>(v);

    if (std::fabs(mV) * kOffsetScale < 0.5) return static_cast<int16_t>(kOffsetMask);

    if (raw < 0) return 0;
    if (raw > kOffsetMask) return static_cast<int16_t>(kOffsetMask);
    return static_cast<int16_t>(raw);
}

double decodeOffsetRaw(int16_t raw) {

    if (raw == 0) return 0.0;

    if (raw == kOffsetMask) return 0.0;

    return -(static_cast<double>(kOffsetBase) - static_cast<double>(raw)) / kOffsetScale;
}

uint64_t packOffsetData(int16_t raw, int plane) {
    return kCmdBase
         | (static_cast<uint64_t>(plane) << kPlaneShift)
         | (static_cast<uint64_t>(static_cast<uint16_t>(raw)) << kRawShift)
         | kWriteFlag;
}

uint64_t packOffsetRead(int plane) {
    return kCmdBase | (static_cast<uint64_t>(plane) << kPlaneShift);
}

double unpackOffsetValue(uint64_t resp) {
    const int16_t raw = static_cast<int16_t>((resp >> kRawShift) & kOffsetMask);
    return decodeOffsetRaw(raw);
}

}
