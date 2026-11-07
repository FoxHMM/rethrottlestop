

#include "undervolt.hpp"
#include "ts_spec.hpp"

#include <cmath>
#include <cpuid.h>
#include <ctime>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <cstring>

namespace uv {

namespace {

bool fivrCommandRaw(MsrBackend* msr, const std::vector<int>& cpus,
                    uint64_t addr, uint32_t command, uint32_t payload,
                    uint64_t* outResponse) {
    const uint64_t writeVal = (static_cast<uint64_t>(command) << 32) | payload;
    for (int cpu : cpus) {
        try {
            msr->write(cpu, static_cast<uint32_t>(addr), writeVal);
        } catch (...) {
            return false;
        }
    }
    const int pollCpu = cpus.empty() ? 0 : cpus.front();
    uint64_t resp = 0;
    for (int i = 0; i < ts::FIVR_POLL_MAX; ++i) {
        try {
            resp = msr->read(pollCpu, static_cast<uint32_t>(addr));
        } catch (...) {
            return false;
        }
        if ((resp & ts::FIVR_STATUS_BUSY) == 0) break;
    }
    if (outResponse) *outResponse = resp;
    return (resp & ts::FIVR_STATUS_BUSY) == 0;
}

bool fivrCommand(MsrBackend* msr, const std::vector<int>& cpus,
                 uint64_t addr, uint32_t command, uint32_t payload,
                 uint64_t* outResponse = nullptr) {
    const uint64_t writeVal = (static_cast<uint64_t>(command) << 32) | payload;

    for (int cpu : cpus) {
        try {
            msr->write(cpu, static_cast<uint32_t>(addr), writeVal);
        } catch (...) {
            return false;
        }
    }

    const int pollCpu = cpus.empty() ? 0 : cpus.front();
    uint64_t resp = 0;
    for (int i = 0; i < ts::FIVR_POLL_MAX; ++i) {
        try {
            resp = msr->read(pollCpu, static_cast<uint32_t>(addr));
        } catch (...) {
            return false;
        }
        if ((resp & ts::FIVR_STATUS_BUSY) == 0) break;
    }

    if (outResponse) *outResponse = resp;

    if ((resp & ts::FIVR_STATUS_BUSY) != 0) return false;
    if ((resp & ts::FIVR_STATUS_CODE_MASK) != 0) return false;

    return true;
}

uint64_t readMsrSafe(MsrBackend* msr, const std::vector<int>& cpus, uint32_t addr,
                     bool* ok) {
    if (!msr || cpus.empty()) {
        if (ok) *ok = false;
        return 0;
    }
    try {
        const uint64_t v = msr->read(cpus.front(), addr);
        if (ok) *ok = true;
        return v;
    } catch (...) {
        if (ok) *ok = false;
        return 0;
    }
}

bool writeMsrAll(MsrBackend* msr, const std::vector<int>& cpus, uint32_t addr,
                 uint64_t value) {
    if (!msr || cpus.empty()) return false;
    if (ts::isMsrBlacklisted(addr)) return false;

    bool any = false;
    for (int cpu : cpus) {
        try {
            msr->write(cpu, addr, value);
            any = true;
        } catch (...) {
        }
    }
    return any;
}

}

std::array<int, 8> Controller::readTurboRatios() {
    std::array<int, 8> out;
    out.fill(-1);

    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_HWP_REQUEST_PKG, &ok);
    if (!ok) return out;

    for (int i = 0; i < ts::TURBO_RATIO_SLOTS; ++i) {
        const int ratio = static_cast<int>((v >> ts::turboRatioShift(i)) & 0xFF);
        out[i] = ratio;
    }
    return out;
}

void Controller::setTurboRatios(const std::array<int, 8>& ratios) {

    bool ok = false;

    int maxRatio = 0;
    for (int i = 0; i < 8; ++i) {
        if (ratios[i] > maxRatio) maxRatio = ratios[i];
    }
    if (maxRatio <= 0) return;

    if (maxRatio > 255) maxRatio = 255;

    const uint64_t old772 = readMsrSafe(msr_.get(), cpus_, 0x772, &ok);
    if (ok) {

        uint64_t v = (old772 & 0xFFFF0000ULL) | (old772 & 0xFFULL)
                   | (static_cast<uint64_t>(maxRatio) << 8);

        v |= (old772 & 0x80000000ULL);

        if (v != old772) {
            if (writeMsrAll(msr_.get(), cpus_, 0x772, v)) {
                const uint64_t back = readMsrSafe(msr_.get(), cpus_, 0x772, &ok);
                if (ok && ((back >> 8) & 0xFF) == static_cast<uint64_t>(maxRatio)) {
                    return;
                }
            }
        } else {
            return;
        }
    }

    const uint64_t old774 = readMsrSafe(msr_.get(), cpus_, 0x774, &ok);
    if (!ok) throw Error("cannot read MSR 0x774");
    uint64_t v = (old774 & ~0xFF00ULL) | (static_cast<uint64_t>(maxRatio) << 8);
    if (!writeMsrAll(msr_.get(), cpus_, 0x774, v)) {
        throw Error("failed to write MSR 0x772/0x774 (Turbo Ratio)");
    }
}

void Controller::setPp0PowerLimit(double watts) {

    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_PKG_POWER_LIMIT, &ok);
    if (!ok) throw Error("cannot read MSR 0x610");

    const uint64_t units = readMsrSafe(msr_.get(), cpus_, ts::MSR_RAPL_POWER_UNIT, &ok);
    const double powerUnit = std::pow(2.0, static_cast<double>(units & 0xF));
    const int raw = static_cast<int>(watts * powerUnit);
    if (raw < 0 || raw > 0x7FFF) throw Error("PP0 power out of range");

    const uint64_t mask = 0x7FFFULL << 32;
    const uint64_t nv = (v & ~mask) | (static_cast<uint64_t>(raw) << 32);
    if (!writeMsrAll(msr_.get(), cpus_, ts::MSR_PKG_POWER_LIMIT, nv)) {
        throw Error("failed to write PP0 power limit");
    }
}

void Controller::setRingPowerLimit(double watts) {

    char path[256];
    std::snprintf(path, sizeof(path),
                  "/sys/class/powercap/intel-rapl:0/constraint_2_power_limit_uw");
    std::ofstream f(path);
    if (f) {
        f << static_cast<uint64_t>(watts * 1e6);
        if (f.good()) return;
    }
    throw Error("Ring power limit not supported on this platform");
}

std::optional<double> Controller::readIccMax(Plane plane) {
    if (!msr_) return std::nullopt;

    uint64_t resp = 0;
    if (!fivrCommandRaw(msr_.get(), cpus_, addrs_.voltageOffsets,
                        ts::FIVR_CMD_READ_ICCMAX,
                        static_cast<uint32_t>(plane) << 16, &resp)) {
        return std::nullopt;
    }
    if ((resp & ts::FIVR_STATUS_BUSY) != 0) return std::nullopt;

    const int raw = static_cast<int>((resp >> 21) & 0x7FF);
    if (raw == 0) return std::nullopt;
    return static_cast<double>(raw) / 4.0;
}

int Controller::readFivrDomainCount() {
    if (!msr_) return 0;
    uint64_t resp = 0;
    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets,
                     ts::FIVR_CMD_DOMAIN_COUNT, 0, &resp)) {
        return 0;
    }
    if ((resp & ts::FIVR_STATUS_BUSY) != 0) return 0;
    return static_cast<int>(resp & 0xFF);
}

std::optional<int> Controller::readCacheRatio() {
    if (!msr_) return std::nullopt;

    uint64_t resp = 0;
    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets,
                     ts::FIVR_CMD_CACHE_RATIO, 0, &resp)) {
        return std::nullopt;
    }
    return static_cast<int>((resp >> 8) & 0xFF);
}

void Controller::setCacheRatio(int ratio) {
    if (!msr_) throw Error("no MSR backend");
    if (ratio < 0 || ratio > 0xFF) throw Error("Cache Ratio out of range");

    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets,
                     ts::FIVR_CMD_CACHE_RATIO,
                     static_cast<uint32_t>(ratio) << 8)) {
        throw Error("Cache Ratio write rejected by BIOS");
    }
}

std::optional<int> Controller::readEpp() {
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_HWP_REQUEST, &ok);
    if (!ok) return std::nullopt;
    return static_cast<int>((v >> 24) & 0xFF);
}

void Controller::setEpp(int epp) {
    if (epp < 0 || epp > 255) throw Error("EPP out of range (0-255)");

    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_HWP_REQUEST, &ok);
    if (!ok) throw Error("cannot read MSR 0x770 (HWP_REQUEST)");

    v |= 1;
    v = (v & ~(0xFFULL << 24)) | (static_cast<uint64_t>(epp) << 24);

    if (!writeMsrAll(msr_.get(), cpus_, ts::MSR_HWP_REQUEST, v)) {
        throw Error("failed to write EPP");
    }
}

void Controller::setVfPoint(Plane plane, int profile, int pointIndex, int mV) {
    if (!msr_) throw Error("no MSR backend");
    if (profile < 0 || profile > 3) throw Error("profile must be 0-3");
    if (pointIndex < 0 || pointIndex > 10) throw Error("point index must be 0-10");

    const uint32_t encoded = ts::vfEncode(mV);

    (void)encoded;
    const uint32_t payload = (static_cast<uint32_t>(plane) << 16) |
                             (static_cast<uint32_t>(profile) << 8) |
                             static_cast<uint32_t>(pointIndex);

    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets,
                     ts::FIVR_CMD_WRITE_VOLTAGE, payload)) {
        throw Error("V/F Point write rejected by BIOS");
    }
}

std::optional<int> Controller::getVfPoint(Plane plane, int profile, int pointIndex) {
    if (!msr_) return std::nullopt;
    if (profile < 0 || profile > 3) return std::nullopt;
    if (pointIndex < 0 || pointIndex > 10) return std::nullopt;

    const uint32_t payload = (static_cast<uint32_t>(plane) << 16) |
                             (static_cast<uint32_t>(profile) << 8) |
                             static_cast<uint32_t>(pointIndex);

    uint64_t resp = 0;
    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets,
                     ts::FIVR_CMD_READ_VOLTAGE, payload, &resp)) {
        return std::nullopt;
    }
    return ts::vfDecode(static_cast<uint32_t>(resp));
}

namespace {

void parseLimitGroup(uint64_t v, Controller::LimitReason::Group& g, bool hasTvbBits) {
    g.raw         = v;
    g.bd_prochot  = (v >> 0)  & 1;
    g.thermal     = (v >> 1)  & 1;
    g.residency   = (v >> 4)  & 1;
    g.avg_thermal = (v >> 5)  & 1;
    g.vr_thermal  = (v >> 6)  & 1;
    g.vr_current  = (v >> 7)  & 1;
    g.edp_other   = (v >> 8)  & 1;
    g.reserved9   = (v >> 9)  & 1;
    g.pl1         = (v >> 10) & 1;
    g.pl2         = (v >> 11) & 1;

    if (hasTvbBits) {
        g.tvb  = (v >> 14) & 1;
        g.vmax = (v >> 15) & 1;
    }
}
}

std::optional<Controller::LimitReason> Controller::readLimitReasons() {

    bool ok = false;
    LimitReason r;

    const uint64_t vCore = readMsrSafe(msr_.get(), cpus_, ts::MSR_PKG_LIMIT_REASONS, &ok);
    if (!ok) {

        const uint64_t vLegacy = readMsrSafe(msr_.get(), cpus_, ts::MSR_LIMIT_REASONS_LEGACY, &ok);
        if (!ok) return std::nullopt;
        parseLimitGroup(vLegacy, r.core, true);
        return r;
    }
    parseLimitGroup(vCore, r.core, true);

    const uint64_t vGpu = readMsrSafe(msr_.get(), cpus_, ts::MSR_PKG_LIMIT_GPU, &ok);
    if (ok) parseLimitGroup(vGpu, r.gpu, false);

    const uint64_t vRing = readMsrSafe(msr_.get(), cpus_, ts::MSR_PKG_LIMIT_RING, &ok);
    if (ok) parseLimitGroup(vRing, r.ring, false);

    return r;
}

void Controller::clearLimitReasons() {

    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_PKG_LIMIT_REASONS, &ok);
    if (!ok) throw Error("cannot read MSR 0x64F");

    const uint64_t cleared = v & ~0xFD30000ULL;
    if (!writeMsrAll(msr_.get(), cpus_, ts::MSR_PKG_LIMIT_REASONS, cleared)) {
        throw Error("failed to clear limit reasons");
    }
}

}

namespace uv {

namespace {
constexpr uint64_t MISC_ENABLE_TURBO_BIT   = (1ULL << 1);
constexpr uint64_t MISC_ENABLE_SPEEDSTEP   = (1ULL << 6);
constexpr uint64_t MISC_ENABLE_BD_PROCHOT  = (1ULL << 11);

bool modifyMsrBit(MsrBackend* msr, const std::vector<int>& cpus, uint32_t addr,
                  uint64_t bit, bool set) {
    if (!msr || cpus.empty() || ts::isMsrBlacklisted(addr)) return false;
    bool ok = false;
    uint64_t v = readMsrSafe(msr, cpus, addr, &ok);
    if (!ok) return false;

    if (set) v |= bit;
    else     v &= ~bit;

    if (!writeMsrAll(msr, cpus, addr, v)) return false;

    const uint64_t back = readMsrSafe(msr, cpus, addr, &ok);
    if (!ok) return false;
    return (back & bit) == (v & bit);
}
}

std::optional<bool> Controller::readTurboEnabled() {
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_MISC_ENABLE, &ok);
    if (!ok) return std::nullopt;
    return (v & MISC_ENABLE_TURBO_BIT) == 0;
}

void Controller::setTurboEnabled(bool on) {

    if (!modifyMsrBit(msr_.get(), cpus_, ts::MSR_MISC_ENABLE,
                      MISC_ENABLE_TURBO_BIT, !on)) {
        throw Error("failed to set Turbo (MSR 0x1A0 bit1 rejected)");
    }
}

std::optional<bool> Controller::readSpeedStep() {
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_MISC_ENABLE, &ok);
    if (!ok) return std::nullopt;
    return (v & MISC_ENABLE_SPEEDSTEP) != 0;
}

void Controller::setSpeedStep(bool on) {
    if (!modifyMsrBit(msr_.get(), cpus_, ts::MSR_MISC_ENABLE,
                      MISC_ENABLE_SPEEDSTEP, on)) {
        throw Error("failed to set SpeedStep (MSR 0x1A0 bit6 rejected)");
    }
}

std::optional<bool> Controller::readBdProchot() {
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_MISC_ENABLE, &ok);
    if (!ok) return std::nullopt;
    return (v & MISC_ENABLE_BD_PROCHOT) != 0;
}

void Controller::setBdProchot(bool on) {
    if (!modifyMsrBit(msr_.get(), cpus_, ts::MSR_MISC_ENABLE,
                      MISC_ENABLE_BD_PROCHOT, on)) {
        throw Error("failed to set BD PROCHOT (MSR 0x1A0 bit11 rejected)");
    }
}

std::optional<Controller::CStateInfo> Controller::readCState() {
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_POWER_CTL, &ok);
    if (!ok) return std::nullopt;

    CStateInfo info;
    info.enabled = (v & 1ULL) != 0;
    info.locked  = (v & (1ULL << 23)) != 0;
    return info;
}

bool Controller::setCState(bool enabled) {
    if (!msr_ || cpus_.empty()) return false;

    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_POWER_CTL, &ok);
    if (!ok) return false;

    if ((v & (1ULL << 23)) != 0) return false;

    if (enabled) {
        v |= 1ULL;
    } else {
        v &= ~1ULL;
        v |= (1ULL << 23);
    }

    return writeMsrAll(msr_.get(), cpus_, ts::MSR_POWER_CTL, v);
}

std::optional<Controller::FreqVid> Controller::readFreqVid() {

    FreqVid out;
    int maxRatio = 0;
    int vidSum = 0;
    int vidN = 0;

    if (!msr_) return std::nullopt;

    for (int cpu : cpus_) {

        uint64_t v = 0;
        try {
            v = msr_->read(cpu, 0x198);
        } catch (...) {
            continue;
        }
        if (v == 0) continue;

        const int ratio = static_cast<int>((v >> 8) & 0xFF);
        if (ratio > maxRatio) maxRatio = ratio;

        const int vidRaw = static_cast<int>((v >> 32) & 0xFFFF);
        if (vidRaw > 0) { vidSum += vidRaw; vidN++; }
    }

    if (maxRatio <= 0) return std::nullopt;

    double bclk = 100.0;
    if (auto b = readBclk()) bclk = *b;

    out.multiplier = static_cast<double>(maxRatio);
    out.vid = vidN > 0
                ? (static_cast<double>(vidSum) / vidN) / 8192.0
                : 0.0;
    out.hsFormat = true;
    return out;
}

std::optional<double> Controller::readBclk() {

    static double cached = 0.0;
    static int64_t lastCalMs = 0;

    struct timespec nowTs;
    clock_gettime(CLOCK_MONOTONIC, &nowTs);
    const int64_t nowMs = static_cast<int64_t>(nowTs.tv_sec) * 1000 +
                          nowTs.tv_nsec / 1000000;
    if (cached > 0.0 && (nowMs - lastCalMs) < 5000) return cached;

    bool ok = false;
    const uint64_t ce = readMsrSafe(msr_.get(), cpus_, 0xCE, &ok);
    if (!ok) return std::nullopt;

    const int maxNonTurboRatio = static_cast<int>((ce >> 8) & 0xFF);
    if (maxNonTurboRatio <= 0) return std::nullopt;

    const uint64_t tsc1 = readMsrSafe(msr_.get(), cpus_, ts::MSR_IA32_TSC, &ok);
    if (!ok) return std::nullopt;

    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    struct timespec req = {0, 20000000};
    nanosleep(&req, nullptr);
    const uint64_t tsc2 = readMsrSafe(msr_.get(), cpus_, ts::MSR_IA32_TSC, &ok);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    if (!ok || tsc2 <= tsc1) return std::nullopt;

    const double dtSec = (t2.tv_sec - t1.tv_sec) +
                         (t2.tv_nsec - t1.tv_nsec) / 1e9;
    if (dtSec <= 0.001) return std::nullopt;

    const double tscMHz = static_cast<double>(tsc2 - tsc1) / dtSec / 1e6;

    const double bclk = tscMHz / static_cast<double>(maxNonTurboRatio);

    if (bclk < 90.0 || bclk > 110.0) return std::nullopt;

    cached = bclk;
    lastCalMs = nowMs;
    return cached;
}

std::optional<int> Controller::readTemperatureC() {
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_THERM_STATUS, &ok);
    if (!ok) return std::nullopt;

    const int dist = static_cast<int>((v >> ts::THERM_DISTANCE_SHIFT)
                                      & ts::THERM_DISTANCE_MASK);
    if (dist == ts::THERM_INVALID) return std::nullopt;

    int tj = 100;
    try {
        int tcc = 0;
        readTemperature(&tcc, &tj);
    } catch (...) {}

    return tj - dist;
}

Controller::RaplPower Controller::sampleRaplPower() {
    RaplPower out;

    auto readUj = [](const char* path) -> std::optional<uint64_t> {
        FILE* f = std::fopen(path, "r");
        if (!f) return std::nullopt;
        unsigned long long v = 0;
        const int n = std::fscanf(f, "%llu", &v);
        std::fclose(f);
        if (n != 1) return std::nullopt;
        return static_cast<uint64_t>(v);
    };

    const auto pkgNow = readUj("/sys/class/powercap/intel-rapl:0/energy_uj");
    const auto dramNow = readUj("/sys/class/powercap/intel-rapl:0:0/energy_uj");
    if (!pkgNow) return out;

    const auto now = std::chrono::steady_clock::now();

    if (raplPrimed_ && lastRaplEnergy_ > 0) {
        const double dt = std::chrono::duration<double>(now - lastRaplTime_).count();
        if (dt > 0.05) {

            const uint64_t prev = static_cast<uint64_t>(lastRaplEnergy_);
            if (*pkgNow >= prev) {
                const uint64_t dPkg = *pkgNow - prev;
                out.pkgW = (static_cast<double>(dPkg) / 1e6) / dt;
                out.ok = out.pkgW > 0.1 && out.pkgW < 600.0;
            } else {

                out.ok = false;
            }

            if (dramNow && lastDramEnergy_ > 0) {
                const uint64_t dprev = static_cast<uint64_t>(lastDramEnergy_);
                if (*dramNow >= dprev) {
                    const uint64_t dDram = *dramNow - dprev;
                    out.dramW = (static_cast<double>(dDram) / 1e6) / dt;
                }
            }
        }
    }

    lastRaplEnergy_ = static_cast<double>(*pkgNow);
    if (dramNow) lastDramEnergy_ = static_cast<double>(*dramNow);
    lastRaplTime_ = now;
    raplPrimed_ = true;
    return out;
}

}

namespace uv {
std::optional<double> Controller::sampleC0Percent() {
    if (!msr_ || cpus_.empty()) return std::nullopt;

    bool ok = false;
    const uint64_t mperf = readMsrSafe(msr_.get(), cpus_, 0xE7, &ok);
    if (!ok) return std::nullopt;
    const uint64_t tsc = readMsrSafe(msr_.get(), cpus_, ts::MSR_IA32_TSC, &ok);
    if (!ok) return std::nullopt;

    static uint64_t prevMperf = 0, prevTsc = 0;
    static bool primed = false;

    if (!primed) {
        prevMperf = mperf;
        prevTsc = tsc;
        primed = true;
        return std::nullopt;
    }

    const uint64_t dM = mperf - prevMperf;
    const uint64_t dT = tsc - prevTsc;
    prevMperf = mperf;
    prevTsc = tsc;

    if (dT == 0) return std::nullopt;
    double pct = (static_cast<double>(dM) * 100.0) / static_cast<double>(dT);
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}
}

namespace uv {

Controller::CoreTopology Controller::readCoreTopology() {
    CoreTopology topo;

    std::vector<std::pair<int,int>> cores;
    for (int cpu = 0; cpu < 256; ++cpu) {
        char path[128];
        std::snprintf(path, sizeof(path),
                      "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
        FILE* f = std::fopen(path, "r");
        if (!f) {
            if (cpu > 0) break;
            continue;
        }
        int khz = 0;
        if (std::fscanf(f, "%d", &khz) == 1) cores.emplace_back(cpu, khz);
        std::fclose(f);
    }
    if (cores.empty()) return topo;

    int globalMax = 0;
    for (auto& [c, f] : cores) if (f > globalMax) globalMax = f;

    for (auto& [cpu, f] : cores) {

        if (f < globalMax * 0.85) {
            topo.eIndex.push_back(cpu);
            if (f > topo.eMaxKHz) topo.eMaxKHz = f;
        } else {
            topo.pIndex.push_back(cpu);
            if (f > topo.pMaxKHz) topo.pMaxKHz = f;
        }
    }
    topo.pCores = static_cast<int>(topo.pIndex.size());
    topo.eCores = static_cast<int>(topo.eIndex.size());
    topo.hybrid = (topo.eCores > 0);
    return topo;
}

std::array<int, 8> Controller::readBiosTurboRatios() {
    std::array<int, 8> out;
    out.fill(0);

    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_TURBO_RATIO_LIMIT, &ok);
    if (!ok) return out;

    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<int>((v >> (i * 8)) & 0xFF);
    }
    return out;
}

}

namespace uv {

Controller::PlatformInfo Controller::readPlatformInfo() {
    PlatformInfo pi;
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_PLATFORM_INFO, &ok);
    if (!ok) return pi;

    pi.maxNonTurboRatio = static_cast<int>((v >> 8) & 0xFF);
    pi.maxRatio         = static_cast<int>((v >> 40) & 0xFF);
    pi.minRatio         = static_cast<int>((v >> 48) & 0xFF);
    pi.ratioLocked      = (v >> 63) & 1;
    pi.programmableRatio = (v >> 28) & 1;
    return pi;
}

Controller::HwpRequest Controller::readHwpRequest() {
    HwpRequest h;
    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_HWP_REQUEST_PKG, &ok);
    if (!ok) {
        v = readMsrSafe(msr_.get(), cpus_, 0x770, &ok);
        if (!ok) return h;
    }

    h.minPerf = static_cast<int>(v & 0xFF);
    h.maxPerf = static_cast<int>((v >> 8) & 0xFF);
    h.desired = static_cast<int>((v >> 16) & 0xFF);
    h.epp     = static_cast<int>((v >> 24) & 0xFF);
    h.ok = true;
    return h;
}

}

namespace uv {
Controller::RingRatio Controller::readRingRatio() {
    RingRatio r;
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x620, &ok);
    if (!ok) return r;

    r.maxRing = static_cast<int>(v & 0x7F);
    r.minRing = static_cast<int>((v >> 8) & 0x7F);
    r.ok = r.maxRing > 0;
    return r;
}
}

namespace uv {

void Controller::setIccMax(Plane plane, double amps) {
    if (!msr_) throw Error("setIccMax: MSR backend unavailable");
    if (amps <= 0 || amps > 1023.75) {
        throw Error("setIccMax: amps out of valid range (0, 1023.75]");
    }

    const uint32_t payload = (static_cast<uint32_t>(amps * 4.0) & 0xFFFF)
                             | (static_cast<uint32_t>(plane) << 16);

    uint64_t resp = 0;
    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets,
                     ts::FIVR_CMD_WRITE_ICCMAX, payload, &resp)) {
        throw Error("setIccMax: FIVR command failed");
    }
    if ((resp & ts::FIVR_STATUS_BUSY) != 0 ||
        (resp & ts::FIVR_STATUS_CODE_MASK) != 0) {
        throw Error("setIccMax: hardware rejected the request "
                    "(FIVR mailbox not supported on this platform)");
    }
}

}

namespace uv {

Controller::MiscFlags Controller::readMiscFlags() {
    MiscFlags m;
    if (!msr_) return m;

    bool ok = false;
    const uint64_t v1ad = readMsrSafe(msr_.get(), cpus_, ts::MSR_TURBO_RATIO_LIMIT, &ok);
    if (ok) {
        m.avx2Offset = static_cast<int>(v1ad & 0xFF);
    }

    uint64_t resp = 0;
    const bool cmdOk = fivrCommandRaw(msr_.get(), cpus_, addrs_.voltageOffsets,
                                      ts::FIVR_CMD_CACHE_RATIO, 0, &resp);
    if (cmdOk && (resp & (1ULL << 31)) == 0) {
        m.ringDown   = (resp >> 2) & 1;
        m.tvbCaps    = (resp >> 3) & 1;
        m.vmaxStress = (resp >> 4) & 1;
        m.tvb        = m.tvbCaps;
        m.tvbKnown   = true;
    }

    m.mvBoost = false;
    m.overclock = false;

    m.ok = true;
    return m;
}

}

namespace uv {

namespace {
std::optional<double> readSysfsDouble(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return std::nullopt;
    double v = 0;
    const int n = std::fscanf(f, "%lf", &v);
    std::fclose(f);
    if (n != 1) return std::nullopt;
    return v;
}

bool writeSysfsDouble(const char* path, double v) {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    const int n = std::fprintf(f, "%.0f\n", v);
    const int rc = std::fclose(f);
    return n > 0 && rc == 0;
}
}

Controller::PowerLimits Controller::readPowerLimits() {
    PowerLimits p;
    const char* base = "/sys/class/powercap/intel-rapl:0";

    char path[256];
    std::snprintf(path, sizeof(path), "%s/constraint_0_power_limit_uw", base);
    if (auto v = readSysfsDouble(path)) p.pl1W = *v / 1e6;

    std::snprintf(path, sizeof(path), "%s/constraint_1_power_limit_uw", base);
    if (auto v = readSysfsDouble(path)) p.pl2W = *v / 1e6;

    std::snprintf(path, sizeof(path), "%s/constraint_0_time_window_us", base);
    if (auto v = readSysfsDouble(path)) p.pl1WindowUs = *v;

    std::snprintf(path, sizeof(path), "%s/constraint_1_time_window_us", base);
    if (auto v = readSysfsDouble(path)) p.pl2WindowUs = *v;

    std::snprintf(path, sizeof(path), "%s/constraint_0_max_power_uw", base);
    if (auto v = readSysfsDouble(path)) p.pl1MaxW = *v / 1e6;

    p.ok = p.pl1W > 0 || p.pl2W > 0;
    return p;
}

bool Controller::setPl1(double watts) {
    if (watts <= 0 || watts > 1000) return false;
    return writeSysfsDouble(
        "/sys/class/powercap/intel-rapl:0/constraint_0_power_limit_uw",
        watts * 1e6);
}

bool Controller::setPl2(double watts) {
    if (watts <= 0 || watts > 1000) return false;
    return writeSysfsDouble(
        "/sys/class/powercap/intel-rapl:0/constraint_1_power_limit_uw",
        watts * 1e6);
}

}

namespace uv {

Controller::CStateConfig Controller::readCStateConfig() {
    CStateConfig cfg;
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, 0xE2, &ok);
    if (!ok) return cfg;

    cfg.cStateEnable  = (v >> 0) & 1;
    cfg.c1Demotion    = (v >> 1) & 1;
    cfg.c3Demotion    = (v >> 2) & 1;
    cfg.c1Undemotion  = (v >> 3) & 1;
    cfg.c3Undemotion  = (v >> 4) & 1;
    cfg.pkgCStateLimit = static_cast<int>((v >> 12) & 0xF);
    cfg.locked        = (v >> 31) & 1;
    cfg.ok = true;
    return cfg;
}

bool Controller::writeCStateConfig(const CStateConfig& cfg) {
    if (!msr_ || cpus_.empty()) return false;

    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, 0xE2, &ok);
    if (!ok) return false;

    if ((v >> 31) & 1) return false;

    auto setBit = [&v](int bit, bool on) {
        if (on) v |= (1ULL << bit);
        else    v &= ~(1ULL << bit);
    };
    setBit(0, cfg.cStateEnable);
    setBit(1, cfg.c1Demotion);
    setBit(2, cfg.c3Demotion);
    setBit(3, cfg.c1Undemotion);
    setBit(4, cfg.c3Undemotion);
    v = (v & ~(0xFULL << 12)) | ((static_cast<uint64_t>(cfg.pkgCStateLimit & 0xF)) << 12);

    if (!writeMsrAll(msr_.get(), cpus_, 0xE2, v)) return false;

    const uint64_t back = readMsrSafe(msr_.get(), cpus_, 0xE2, &ok);
    return ok && back == v;
}

std::vector<int> Controller::readPerCoreRatios() {
    std::vector<int> out;
    if (!msr_) return out;

    for (int cpu : cpus_) {
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/cpu/%d/msr", cpu);
        int fd = ::open(path, O_RDONLY);
        if (fd < 0) { out.push_back(0); continue; }
        uint64_t v = 0;
        const ssize_t n = ::pread(fd, &v, 8, 0x198);
        ::close(fd);
        out.push_back(n == 8 ? static_cast<int>((v >> 8) & 0xFF) : 0);
    }
    return out;
}

std::vector<int> Controller::readPerCoreDefaults() {

    std::vector<int> out;
    const auto bios = readBiosTurboRatios();
    const auto topo = readCoreTopology();

    for (size_t i = 0; i < cpus_.size(); ++i) {
        int idx = 7;
        if (topo.hybrid) {

            if (static_cast<int>(i) < topo.pCores) {
                idx = std::min(static_cast<int>(i), 7);
            } else {
                idx = 7;
            }
        } else {
            idx = std::min(static_cast<int>(i), 7);
        }
        out.push_back(bios[idx] > 0 ? bios[idx] : 0);
    }
    return out;
}

}

namespace uv {

Controller::TurboPower610 Controller::readTurboPower610() {
    TurboPower610 tp;
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x610, &ok);
    if (!ok) return tp;

    tp.pl1W      = static_cast<double>(v & 0x7FFF) / 8.0;
    tp.pl1Enable = (v >> 15) & 1;
    tp.pl1Window = static_cast<int>((v >> 17) & 0x7F);
    tp.pl2W      = static_cast<double>((v >> 32) & 0x7FFF) / 8.0;
    tp.pl2Enable = (v >> 47) & 1;
    tp.pl2Window = static_cast<int>((v >> 49) & 0x7F);
    tp.ok = tp.pl1W > 0;
    return tp;
}

bool Controller::writeTurboPower610(const TurboPower610& p) {
    if (!msr_ || cpus_.empty()) return false;
    if (p.pl1W <= 0 || p.pl1W > 4095 || p.pl2W <= 0 || p.pl2W > 4095) return false;

    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x610, &ok);
    if (!ok) return false;

    const uint64_t pl1Raw = static_cast<uint64_t>(p.pl1W * 8.0) & 0x7FFF;
    v = (v & ~0x7FFFULL) | pl1Raw;
    v = p.pl1Enable ? (v | (1ULL << 15)) : (v & ~(1ULL << 15));
    v = (v & ~(0x7FULL << 17)) | ((static_cast<uint64_t>(p.pl1Window & 0x7F)) << 17);

    const uint64_t pl2Raw = static_cast<uint64_t>(p.pl2W * 8.0) & 0x7FFF;
    v = (v & ~(0x7FFFULL << 32)) | (pl2Raw << 32);
    v = p.pl2Enable ? (v | (1ULL << 47)) : (v & ~(1ULL << 47));
    v = (v & ~(0x7FULL << 49)) | ((static_cast<uint64_t>(p.pl2Window & 0x7F)) << 49);

    if (!writeMsrAll(msr_.get(), cpus_, 0x610, v)) return false;

    const uint64_t back = readMsrSafe(msr_.get(), cpus_, 0x610, &ok);
    return ok && back == v;
}

Controller::ClockMod Controller::readClockMod() {
    ClockMod cm;
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x19A, &ok);
    if (!ok) return cm;

    cm.enabled   = v & 1;
    cm.dutyCycle = static_cast<int>((v >> 1) & 0xF);
    cm.ok = true;
    return cm;
}

bool Controller::writeClockMod(int dutyPercent) {
    if (!msr_ || cpus_.empty()) return false;
    if (dutyPercent < 0 || dutyPercent > 100) return false;

    int duty = 0;
    bool enable = false;
    if (dutyPercent < 100) {
        duty = (15 * (100 - dutyPercent) + 50) / 100;
        if (duty < 1) duty = 1;
        if (duty > 15) duty = 15;
        enable = true;
    }

    uint64_t v = enable ? 1ULL : 0ULL;
    v |= (static_cast<uint64_t>(duty) & 0xF) << 1;

    if (!writeMsrAll(msr_.get(), cpus_, 0x19A, v)) return false;
    bool ok = false;
    const uint64_t back = readMsrSafe(msr_.get(), cpus_, 0x19A, &ok);
    return ok && back == v;
}

}

namespace uv {
uint64_t Controller::readLimitReasonsRaw() {
    if (!msr_) return 0;
    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_PKG_LIMIT_REASONS, &ok);
    if (!ok) {
        v = readMsrSafe(msr_.get(), cpus_, ts::MSR_LIMIT_REASONS_LEGACY, &ok);
        if (!ok) return 0;
    }
    return v;
}
}

namespace uv {

std::optional<bool> Controller::readSlfm() {

    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, 0xEE, &ok);
    if (!ok) return std::nullopt;
    return (v >> 28) & 1;
}

bool Controller::setSlfm(bool on) {
    if (!msr_ || cpus_.empty()) return false;

    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, 0xEE, &ok);
    if (!ok) return false;

    if (on) v |= (1ULL << 28);
    else    v &= ~(1ULL << 28);

    if (!writeMsrAll(msr_.get(), cpus_, 0xEE, v)) return false;

    const uint64_t back = readMsrSafe(msr_.get(), cpus_, 0xEE, &ok);
    return ok && ((back >> 28) & 1) == (on ? 1u : 0u);
}

}

namespace uv {

std::vector<Controller::PerCoreSample> Controller::samplePerCore() {
    std::vector<PerCoreSample> out;
    if (!msr_) return out;

    ClockMod cm = readClockMod();
    double modPct = 100.0;
    if (cm.ok && cm.enabled) {
        modPct = 100.0 * (16 - cm.dutyCycle) / 16.0;
    }

    double bclk = 100.0;
    if (auto b = readBclk()) bclk = *b;

    int tjmax = 100;
    bool tjOk = false;
    {
        const uint64_t v = readMsrSafe(msr_.get(), cpus_, ts::MSR_TEMPERATURE_TARGET, &tjOk);
        if (tjOk) tjmax = static_cast<int>((v >> 16) & 0xFF);
    }

    static std::vector<uint64_t> prevC0;
    static std::vector<uint64_t> prevAperf;
    static std::vector<uint64_t> prevMperf;
    static std::vector<uint64_t> prevTsc;
    static bool firstFrame = true;

    for (size_t idx = 0; idx < cpus_.size(); ++idx) {
        const int cpu = cpus_[idx];
        PerCoreSample s;
        s.cpu = cpu;

        if (!msr_) {
            out.push_back(s);
            continue;
        }

        uint64_t perfStatus = 0, thermStatus = 0, tsc = 0, c0cnt = 0;
        try {
            perfStatus = msr_->read(cpu, 0x198);
            thermStatus = msr_->read(cpu, ts::MSR_THERM_STATUS);
            tsc = msr_->read(cpu, ts::MSR_IA32_TSC);
            c0cnt = msr_->read(cpu, ts::MSR_C0_COUNTER_1);
        } catch (...) {
            out.push_back(s);
            continue;
        }

        if (perfStatus == 0) {
            out.push_back(s);
            continue;
        }
        const bool gotTherm = thermStatus != 0;
        const bool gotTsc = tsc != 0;
        const bool gotC0 = c0cnt != 0;

        s.fid = static_cast<int>((perfStatus >> 8) & 0xFF);
        s.freqMhz = static_cast<double>(s.fid) * bclk;
        s.modPct = modPct;

        if (gotTherm) {
            const int dist = static_cast<int>((thermStatus >> ts::THERM_DISTANCE_SHIFT)
                                              & ts::THERM_DISTANCE_MASK);
            if (dist != ts::THERM_INVALID) {
                s.tempC = tjmax - dist;
            }
        }

        if (prevC0.size() < cpus_.size()) {
            prevC0.resize(cpus_.size(), 0);
            prevTsc.resize(cpus_.size(), 0);
            prevAperf.resize(cpus_.size(), 0);
            prevMperf.resize(cpus_.size(), 0);
        }
        {
            uint64_t counter = 0;
            bool haveCounter = false;

            if (gotC0 && c0cnt != 0) {
                counter = c0cnt & ts::C0_COUNTER_MASK;
                haveCounter = true;
            }

            uint64_t aperf = 0, mperf = 0;
            try {
                aperf = msr_->read(cpu, 0xE8);
                mperf = msr_->read(cpu, 0xE7);
            } catch (...) {}
            const bool haveAperfMperf = (aperf != 0 && mperf != 0);

            if (haveAperfMperf && prevAperf[idx] != 0 && prevMperf[idx] != 0) {

                const uint64_t dA = aperf - prevAperf[idx];
                const uint64_t dM = mperf - prevMperf[idx];
                if (dM > 0) {
                    double pct = 100.0 * static_cast<double>(dA)
                                       / static_cast<double>(dM);
                    if (pct > 100.0) pct = 100.0;
                    if (pct < 0.0) pct = 0.0;
                    s.c0pct = pct;
                }
            } else if (haveCounter && gotTsc && prevTsc[idx] != 0 && tsc > prevTsc[idx]) {

                const uint64_t dc = counter - prevC0[idx];
                const uint64_t dt = tsc - prevTsc[idx];
                if (dt > 0 && dc <= dt) {
                    s.c0pct = 100.0 * static_cast<double>(dc) / static_cast<double>(dt);
                    if (s.c0pct > 100.0) s.c0pct = 100.0;
                }
            }
            if (haveCounter) prevC0[idx] = counter;
            if (gotTsc) prevTsc[idx] = tsc;
            if (haveAperfMperf) {
                prevAperf[idx] = aperf;
                prevMperf[idx] = mperf;
            }
        }

        s.valid = true;
        out.push_back(s);
    }

    firstFrame = false;
    return out;
}

}

namespace uv {

std::optional<int> Controller::readEpb() {
    bool ok = false;
    const uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x1B1, &ok);
    if (!ok) return std::nullopt;
    return static_cast<int>(v & 0xF);
}

bool Controller::fixEpb() {
    if (!msr_ || cpus_.empty()) return false;

    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x1B1, &ok);
    if (!ok) return false;

    if ((v & 0x3) != 0x2) return true;

    v &= ~(1ULL << 1);
    return writeMsrAll(msr_.get(), cpus_, 0x1B1, v);
}

}

namespace uv {

std::optional<uint32_t> Controller::readMicrocodeRevision() {
    if (!msr_ || cpus_.empty()) return std::nullopt;

    bool ok = false;
    (void)readMsrSafe(msr_.get(), cpus_, 0x8B, &ok);
    if (!ok) return std::nullopt;

    const uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x8B, &ok);
    if (!ok) return std::nullopt;

    const uint32_t rev = static_cast<uint32_t>(v >> 32);
    return rev == 0 ? std::nullopt : std::optional<uint32_t>(rev);
}

}

namespace uv {

bool Controller::writeAvxOffset(int value) {

    (void)value;
    return false;
}

bool Controller::tvbWritable() {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (__get_cpuid(1, &a, &b, &c, &d) == 0) return false;

    static const unsigned kList[] = {
        0x906A0u, 0xB0670u, 0xB06A0u, 0xB06E0u, 0xB06F0u, 0xC0660u
    };
    for (unsigned v : kList) {
        if (a == v) return true;
    }
    return false;
}

bool Controller::writeTvb(bool enabled) {

    if (!tvbWritable()) return false;

    if (!msr_ || cpus_.empty()) return false;

    bool ok = false;
    uint64_t v = readMsrSafe(msr_.get(), cpus_, 0x1AE, &ok);
    if (!ok) return false;

    const uint64_t desired = enabled ? (v | 1ULL) : (v & ~1ULL);
    if (desired == v) return true;

    if (!writeMsrAll(msr_.get(), cpus_, 0x1AE, desired)) return false;
    const uint64_t back = readMsrSafe(msr_.get(), cpus_, 0x1AE, &ok);
    return ok && ((back & 1) == (enabled ? 1u : 0u));
}

bool Controller::writeRingDownBin(bool enabled) {

    (void)enabled;
    return false;
}

}

namespace uv {

std::optional<double> Controller::readDomainVoltage(int domain) {
    if (!msr_ || cpus_.empty()) return std::nullopt;
    if (domain < 0 || domain > 5) return std::nullopt;

    const uint32_t cmd = 0x80000004u | (static_cast<uint32_t>(domain) << 8);

    uint64_t resp = 0;
    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets, cmd, 0, &resp)) {
        return std::nullopt;
    }

    if ((resp & 0x1F) != 0) return std::nullopt;

    if ((resp >> 31) & 1) return std::nullopt;

    const uint32_t raw = static_cast<uint32_t>((resp >> 8) & 0x7FF);
    if (raw == 0) return std::nullopt;

    return static_cast<double>(raw) / 1024.0;
}

std::optional<bool> Controller::readDomainWritable(int domain) {
    if (!msr_ || cpus_.empty()) return std::nullopt;
    if (domain < 0 || domain > 5) return std::nullopt;

    const uint32_t cmd = 0x80000004u | (static_cast<uint32_t>(domain) << 8);

    uint64_t resp = 0;
    if (!fivrCommand(msr_.get(), cpus_, addrs_.voltageOffsets, cmd, 0, &resp)) {
        return std::nullopt;
    }
    if ((resp & 0x1F) != 0) return std::nullopt;

    return ((resp >> 20) & 1) != 0;
}

}

namespace uv {

std::optional<double> Controller::readIccMaxByDomain(int domain) {
    if (!msr_ || cpus_.empty()) return std::nullopt;
    if (domain < 0 || domain > 5) return std::nullopt;

    const int cpu = cpus_.front();
    const auto msr = static_cast<uint32_t>(addrs_.voltageOffsets);

    const uint64_t cmd = (0x80000010ULL | (static_cast<uint64_t>(domain) << 8)) << 32;
    msr_->write(cpu, msr, cmd);
    usleep(1000);

    uint64_t resp = 0;
    for (int i = 0; i < 50; ++i) {
        resp = msr_->read(cpu, msr);
        if ((resp & 0x80000000ULL) == 0) break;
        usleep(1000);
    }

    if ((resp & 0x1F) != 0) return std::nullopt;

    const uint32_t raw = static_cast<uint32_t>(resp & 0xFF);
    if (raw == 0) return std::nullopt;
    return static_cast<double>(raw) * 0.25;
}

bool Controller::writeIccMaxByDomain(int domain, double amps) {
    if (!msr_ || cpus_.empty()) return false;
    if (domain < 0 || domain > 5) return false;
    if (amps <= 0 || amps > 63.75) return false;

    const int cpu = cpus_.front();
    const auto msr = static_cast<uint32_t>(addrs_.voltageOffsets);
    const uint32_t target = static_cast<uint32_t>(amps / 0.25 + 0.5);

    const uint64_t payload = ((0x80000011ULL | (static_cast<uint64_t>(domain) << 8)) << 32)
                             | target;

    const uint64_t rcmd = (0x80000010ULL | (static_cast<uint64_t>(domain) << 8)) << 32;
    msr_->write(cpu, msr, rcmd);
    usleep(1000);
    uint64_t cur = 0;
    for (int i = 0; i < 50; ++i) {
        cur = msr_->read(cpu, msr);
        if ((cur & 0x80000000ULL) == 0) break;
        usleep(1000);
    }
    if ((cur & 0x1F) == 0 && static_cast<uint32_t>(cur & 0xFF) == target) {
        return true;
    }

    msr_->write(cpu, msr, payload);
    usleep(1000);

    msr_->write(cpu, msr, rcmd);
    usleep(1000);
    const uint64_t back = msr_->read(cpu, msr);
    return (back & 0x1F) == 0 && static_cast<uint32_t>(back & 0xFF) == target;
}

}

namespace uv {

std::optional<int> Controller::readCacheRatioByDomain(int domain) {
    if (!msr_ || cpus_.empty()) return std::nullopt;
    if (domain < 0 || domain > 5) return std::nullopt;

    const int cpu = cpus_.front();
    const auto msr = static_cast<uint32_t>(addrs_.voltageOffsets);

    const uint64_t cmd = (0x80000018ULL | (static_cast<uint64_t>(domain) << 8)) << 32;
    msr_->write(cpu, msr, cmd);
    usleep(1000);

    uint64_t resp = 0;
    for (int i = 0; i < 50; ++i) {
        resp = msr_->read(cpu, msr);
        if ((resp & 0x80000000ULL) == 0) break;
        usleep(1000);
    }

    if ((resp & 0x1F) != 0) return std::nullopt;

    const int ratio = static_cast<int>(resp & 0xF);
    if (ratio == 0) return std::nullopt;
    return ratio;
}

}

namespace uv {

bool Controller::writeOffsetByDomain(int domain, double mV) {
    if (!msr_ || cpus_.empty()) return false;
    if (domain < 0 || domain > 5) return false;
    if (mV > 0) return false;

    const int cpu = cpus_.front();
    const auto msr = static_cast<uint32_t>(addrs_.voltageOffsets);
    const int16_t raw = encodeOffsetRaw(mV);

    const uint64_t payload = ((0x80000011ULL | (static_cast<uint64_t>(domain) << 8)) << 32)
                             | (static_cast<uint64_t>(static_cast<uint16_t>(raw)) << 21);
    msr_->write(cpu, msr, payload);
    usleep(1000);

    const uint64_t rcmd = (0x80000010ULL | (static_cast<uint64_t>(domain) << 8)) << 32;
    msr_->write(cpu, msr, rcmd);
    usleep(1000);
    const uint64_t resp = msr_->read(cpu, msr);
    if (resp == 0) return true;
    const int16_t back = static_cast<int16_t>((resp >> 21) & 0x7FF);
    return back == raw;
}

}

namespace uv {

bool Controller::unlockVoltage() {
    if (!msr_ || cpus_.empty()) return false;

    const int cpu = cpus_.front();
    const auto msr = static_cast<uint32_t>(addrs_.voltageOffsets);

    msr_->write(cpu, msr, 0x80000012ULL << 32);
    usleep(2000);

    uint64_t resp = 0;
    for (int i = 0; i < 50; ++i) {
        resp = msr_->read(cpu, msr);
        if ((resp & 0x80000000ULL) == 0) break;
        usleep(1000);
    }

    return (resp & 1) != 0;
}

bool Controller::zeroAllOffsets() {
    if (!msr_ || cpus_.empty()) return false;

    bool allOk = true;
    for (int p = 0; p < kPlaneCount; ++p) {
        try {
            setOffset(static_cast<Plane>(p), 0.0, true);
        } catch (...) {
            allOk = false;
        }
        usleep(20000);
    }
    return allOk;
}

}

namespace uv {

namespace {

bool writeRatioField(MsrBackend* msr, const std::vector<int>& cpus,
                     uint32_t reg, int ratio) {
    bool ok = false;
    const uint64_t old = readMsrSafe(msr, cpus, reg, &ok);
    if (!ok) return false;

    const uint64_t v = (old & 0xFFFF0000ULL)
                     | (old & 0xFFULL)
                     | (static_cast<uint64_t>(ratio & 0xFF) << 8)
                     | (old & 0x80000000ULL);

    if (v == old) return true;

    if (!writeMsrAll(msr, cpus, reg, v)) return false;

    const uint64_t back = readMsrSafe(msr, cpus, reg, &ok);
    return ok && ((back >> 8) & 0xFF) == static_cast<uint64_t>(ratio & 0xFF);
}

static constexpr struct { int ratio; int freq; } kRatioTable[] = {
    {36, 28}, {38, 29}, {40, 31}, {42, 33}, {44, 34}, {46, 36},
    {48, 37}, {50, 39}, {52, 40}, {54, 42}, {56, 44}, {58, 45},
    {60, 47}, {62, 48}, {64, 48}, {66, 50},
};
constexpr int kRatioTableLen = sizeof(kRatioTable) / sizeof(kRatioTable[0]);

}
int Controller::freqToRatio(int targetFreq) {
    if (targetFreq <= kRatioTable[0].freq) return kRatioTable[0].ratio;
    if (targetFreq >= kRatioTable[kRatioTableLen - 1].freq)
        return kRatioTable[kRatioTableLen - 1].ratio;

    int best = kRatioTable[0].ratio;
    int bestErr = 9999;
    for (int i = 0; i < kRatioTableLen; ++i) {
        const int err = kRatioTable[i].freq - targetFreq;
        const int absErr = err < 0 ? -err : err;
        if (absErr < bestErr) {
            bestErr = absErr;
            best = kRatioTable[i].ratio;
            if (absErr == 0) break;
        }
    }
    return best;
}

int Controller::ratioToFreq(int ratio) {

    int best = kRatioTable[0].freq;
    int bestErr = 9999;
    for (int i = 0; i < kRatioTableLen; ++i) {
        const int err = kRatioTable[i].ratio - ratio;
        const int absErr = err < 0 ? -err : err;
        if (absErr < bestErr) {
            bestErr = absErr;
            best = kRatioTable[i].freq;
            if (absErr == 0) break;
        }
    }
    return best;
}

namespace {

bool writeRatioPerCore(MsrBackend* msr, const std::vector<int>& cpus,
                       int ratio, int* okCount, int* failCount) {
    if (!msr || cpus.empty()) return false;
    if (ratio <= 0 || ratio > 255) return false;

    int ok = 0, fail = 0;
    for (int cpu : cpus) {
        try {
            const uint64_t cur = msr->read(cpu, 0x774);

            const uint64_t nv = (cur & ~0xFF00ULL)
                              | (static_cast<uint64_t>(ratio) << 8);
            if (nv == cur) { ++ok; continue; }
            msr->write(cpu, 0x774, nv);
            ++ok;
        } catch (...) {
            ++fail;
        }
    }
    if (okCount) *okCount = ok;
    if (failCount) *failCount = fail;
    return ok > 0;
}
}

void Controller::setTurboRatioP(int targetFreq) {
    if (!msr_ || cpus_.empty()) throw Error("no MSR backend");

    if (targetFreq <= 0 || targetFreq > 100) throw Error("freq out of range");

    const auto topo = readCoreTopology();
    const std::vector<int>& targets = topo.hybrid ? topo.pIndex : cpus_;
    if (targets.empty()) throw Error("no P-cores found");

    const int ratio = freqToRatio(targetFreq);

    int ok = 0, fail = 0;
    if (!writeRatioPerCore(msr_.get(), targets, ratio, &ok, &fail)) {
        throw Error("failed to write MSR 0x774 on P-cores");
    }
    if (ok == 0) throw Error("all P-core writes rejected");
}

void Controller::setTurboRatioE(int targetFreq) {
    if (!msr_ || cpus_.empty()) throw Error("no MSR backend");
    if (targetFreq <= 0 || targetFreq > 100) throw Error("freq out of range");

    const auto topo = readCoreTopology();
    if (!topo.hybrid || topo.eIndex.empty()) {
        throw Error("no E-cores found on this platform");
    }

    int clamped = targetFreq;
    if (clamped > 37) clamped = 37;
    const int ratio = freqToRatio(clamped);

    int ok = 0, fail = 0;
    if (!writeRatioPerCore(msr_.get(), topo.eIndex, ratio, &ok, &fail)) {
        throw Error("failed to write MSR 0x774 on E-cores");
    }
    if (ok == 0) throw Error("all E-core writes rejected");
}

void Controller::setSpeedShiftEpp(int epp) {
    if (!msr_ || cpus_.empty()) throw Error("no MSR backend");
    if (epp < 0 || epp > 255) throw Error("EPP out of range (0-255)");

    const uint64_t v = (static_cast<uint64_t>(epp) << 8)
                     | static_cast<uint64_t>(epp);
    if (!writeMsrAll(msr_.get(), cpus_, 0x772, v)) {
        throw Error("failed to write MSR 0x772 (Speed Shift EPP)");
    }
    bool ok = false;
    const uint64_t back = readMsrSafe(msr_.get(), cpus_, 0x772, &ok);
    if (!ok || ((back >> 8) & 0xFF) != static_cast<uint64_t>(epp)) {
        throw Error("Speed Shift EPP write rejected");
    }
}

}
