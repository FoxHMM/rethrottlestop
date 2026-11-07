

#ifndef UV_TS_SPEC_HPP
#define UV_TS_SPEC_HPP

#include <cmath>
#include <cstdint>

namespace uv {
namespace ts {

constexpr uint32_t MSR_PLATFORM_INFO      = 0x00CE;
constexpr uint32_t MSR_PMG_IO_CAPTURE     = 0x00E2;
constexpr uint32_t MSR_FIVR_MAILBOX       = 0x0150;
constexpr uint32_t MSR_PERF_CTL           = 0x0186;
constexpr uint32_t MSR_THERM_STATUS       = 0x019C;
constexpr uint32_t MSR_MISC_ENABLE        = 0x01A0;
constexpr uint32_t MSR_TEMPERATURE_TARGET = 0x01A2;
constexpr uint32_t MSR_TURBO_RATIO_LIMIT  = 0x01AD;
constexpr uint32_t MSR_TVB                 = 0x01AE;
constexpr uint32_t MSR_POWER_CTL           = 0x01FC;
constexpr uint32_t MSR_PKG_ENERGY_STATUS   = 0x030A;
constexpr uint32_t MSR_DRAM_ENERGY_STATUS  = 0x030B;
constexpr uint32_t MSR_RAPL_POWER_UNIT     = 0x0606;
constexpr uint32_t MSR_PKG_POWER_LIMIT     = 0x0610;
constexpr uint32_t MSR_PP0_CURRENT_LIMIT   = 0x0638;
constexpr uint32_t MSR_PKG_CST_CONFIG      = 0x064B;
constexpr uint32_t MSR_PKG_LIMIT_REASONS   = 0x064F;
constexpr uint32_t MSR_PKG_LIMIT_GPU       = 0x06B0;
constexpr uint32_t MSR_PKG_LIMIT_RING      = 0x06B1;

struct LimitBitDef { int bit; uint16_t ctrlId; const char* name; };

constexpr LimitBitDef LIMIT_BITS_CORE[12] = {
    { 0, 0x601, "BD PROCHOT"},
    { 1, 0x603, "THERMAL"},
    { 4, 0x604, "RESIDENCY"},
    { 5, 0x605, "AVG THERMAL"},
    { 6, 0x607, "VR THERMAL"},
    { 7, 0x608, "VR CURRENT"},
    { 8, 0x609, "EDP OTHER"},
    { 9, 0x61B, "RESERVED"},
    {10, 0x60A, "PL 1"},
    {11, 0x60B, "PL 2"},
    {14, 0x6D1, "TVB"},
    {15, 0x6D5, "V MAX"},
};

constexpr LimitBitDef LIMIT_BITS_GPURING[10] = {
    { 0, 0x612, "BD PROCHOT"},
    { 1, 0x613, "THERMAL"},
    { 4, 0x614, "RESERVED"},
    { 5, 0x615, "AVG THERMAL"},
    { 6, 0x616, "VR THERMAL"},
    { 7, 0x61C, "VR CURRENT"},
    { 8, 0x618, "EDP OTHER"},
    { 9, 0x619, "RESERVED"},
    {10, 0x61A, "PL 1"},
    {11, 0x617, "PL 2"},
};
constexpr uint32_t MSR_HWP_REQUEST         = 0x0770;
constexpr uint32_t MSR_HWP_STATUS          = 0x0771;
constexpr uint32_t MSR_HWP_CAPABILITIES    = 0x0772;
constexpr uint32_t MSR_HWP_REQUEST_PKG     = 0x0774;
constexpr uint32_t MSR_IA32_THERM_STATUS   = 0x0194;

constexpr uint32_t MSR_LIMIT_REASONS_LEGACY = 0x0690;
constexpr uint32_t MSR_LIMIT_REASONS_2      = 0x06B0;
constexpr uint32_t MSR_LIMIT_REASONS_3      = 0x06B1;

constexpr uint32_t FIVR_CMD_INIT          = 0x80000001;
constexpr uint32_t FIVR_CMD_RESET         = 0x80000002;
constexpr uint32_t FIVR_CMD_READ_CAPS     = 0x80000004;
constexpr uint32_t FIVR_CMD_READ_ICCMAX   = 0x80000010;

constexpr uint32_t FIVR_CMD_DOMAIN_COUNT  = 0x80010010;
constexpr uint32_t FIVR_CMD_WRITE_ICCMAX  = 0x80000011;
constexpr uint32_t FIVR_CMD_UNLOCK        = 0x80000012;
constexpr uint32_t FIVR_CMD_CACHE_RATIO   = 0x80000018;
constexpr uint32_t FIVR_CMD_COMMIT_FLAGS  = 0x80000019;
constexpr uint32_t FIVR_CMD_READ_VOLTAGE  = 0x8000001A;
constexpr uint32_t FIVR_CMD_WRITE_VOLTAGE = 0x8000001B;
constexpr uint32_t FIVR_CMD_POWERCUT      = 0x8000001C;

constexpr uint32_t FIVR_CMD_INIT_4DOMAIN  = 0x80000102;
constexpr uint32_t FIVR_CMD_INIT_8DOMAIN  = 0x80000202;

constexpr uint64_t FIVR_STATUS_BUSY = (1ULL << 31);
constexpr uint64_t FIVR_STATUS_CODE_MASK = 0x1F;
constexpr int FIVR_POLL_MAX = 50;

constexpr int FIVR_MAX_DOMAINS = 6;

constexpr int TURBO_RATIO_SLOTS = 8;
constexpr uint64_t TURBO_RATIO_RESERVED_MASK = 0x7FFFBFF;

inline int turboRatioShift(int slot) { return slot * 8; }

constexpr uint64_t PL1_POWER_MASK   = 0x7FFF;
constexpr uint64_t PL1_ENABLE       = (1ULL << 15);
constexpr uint64_t PL1_TIME_SHIFT   = 17;
constexpr uint64_t PL1_TIME_MASK    = (0x1FULL << 17);
constexpr uint64_t PL1_CLAMP        = (1ULL << 31);
constexpr uint64_t PL2_POWER_MASK   = (0x7FFFULL << 32);
constexpr uint64_t PL2_ENABLE       = (1ULL << 47);
constexpr uint64_t PL2_TIME_SHIFT   = 49;
constexpr uint64_t PL2_TIME_MASK    = (0x1FULL << 49);
constexpr uint64_t PL_LOCK          = (1ULL << 63);

constexpr int RAPL_POWER_UNIT_SHIFT  = 0;
constexpr int RAPL_ENERGY_UNIT_SHIFT = 8;
constexpr int RAPL_TIME_UNIT_SHIFT   = 16;

constexpr int THERM_DISTANCE_SHIFT = 16;
constexpr uint64_t THERM_DISTANCE_MASK = 0x7F;
constexpr int THERM_INVALID = 0x7F;

constexpr int TJMAX_OFFSET_SHIFT = 24;
constexpr uint64_t TJMAX_OFFSET_MASK = 0xF;

constexpr int32_t VF_NEGATIVE_MARKER = 0xFFFFFC00;
constexpr int VF_SHIFT = 21;

inline uint32_t vfEncode(int mV) {
    int32_t raw;
    if (mV >= 0) {
        raw = ((mV * 1024) + 512) / 1000;
    } else {
        const int32_t t = (512 - (mV * 1024)) / 1000;
        raw = -t;
        raw |= VF_NEGATIVE_MARKER;
    }
    return static_cast<uint32_t>(raw) << VF_SHIFT;
}

inline int vfDecode(uint32_t stored) {

    const uint32_t bits = (stored >> VF_SHIFT) & 0xFFF;

    int rounded;
    if (bits <= 1024) {
        rounded = static_cast<int>(bits);
    } else {
        rounded = -(2048 - static_cast<int>(bits));
    }

    return static_cast<int>(std::llround(static_cast<double>(rounded) / 1.024));
}

constexpr uint32_t MSR_BLACKLIST[] = {
    0xC0000080,
    0xC0000084,
    0x0000001B,
    0x00000277,
    0x000001A0,
    0xC0000103,
};

inline bool isMsrBlacklisted(uint32_t msr) {
    for (uint32_t m : MSR_BLACKLIST) {
        if (m == msr) return true;
    }
    return false;
}

constexpr uint32_t MSR_C0_COUNTER_1 = 0x630;
constexpr uint32_t MSR_C0_COUNTER_2 = 0x631;
constexpr uint32_t MSR_C0_COUNTER_3 = 0x632;
constexpr uint64_t C0_COUNTER_MASK = 0x0FFFFFFF;
constexpr uint32_t MSR_IA32_TSC = 0x10;

enum class Platform : int {
    Unknown = 0,
    Gen1 = 1,
    Gen2 = 2,
    Gen4 = 4,
    Gen5 = 5,
};

}
}

#endif
