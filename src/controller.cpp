

#include "undervolt.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace uv {

namespace {

constexpr const char* kRaplBase = "/sys/class/powercap/intel-rapl:0";
constexpr const char* kNoTurboPath = "/sys/devices/system/cpu/intel_pstate/no_turbo";

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::string line;
    if (!std::getline(f, line)) return std::nullopt;
    return trim(line);
}

bool writeFile(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    if (!f) return false;
    f << value;
    return f.good();
}

bool writeRaplLimit(int constraint, uint64_t uw) {
    char path[256];
    std::snprintf(path, sizeof(path),
                  "%s/constraint_%d_power_limit_uw", kRaplBase, constraint);
    return writeFile(path, std::to_string(uw));
}

bool writeRaplWindow(int constraint, uint64_t us) {
    char path[256];
    std::snprintf(path, sizeof(path),
                  "%s/constraint_%d_time_window_us", kRaplBase, constraint);
    return writeFile(path, std::to_string(us));
}

std::optional<uint64_t> readRaplLimit(int constraint) {
    char path[256];
    std::snprintf(path, sizeof(path),
                  "%s/constraint_%d_power_limit_uw", kRaplBase, constraint);
    auto v = readFile(path);
    if (!v) return std::nullopt;
    try {
        return std::stoull(*v);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<uint64_t> readRaplWindow(int constraint) {
    char path[256];
    std::snprintf(path, sizeof(path),
                  "%s/constraint_%d_time_window_us", kRaplBase, constraint);
    auto v = readFile(path);
    if (!v) return std::nullopt;
    try {
        return std::stoull(*v);
    } catch (...) {
        return std::nullopt;
    }
}

constexpr int kRaplLong = 0;
constexpr int kRaplShort = 1;

}

Controller::Controller(MsrAddresses addrs) : addrs_(addrs) {
    std::string diag;

    msr_ = createDaemonBackend(&diag);

    if (!msr_) {
        msr_ = createMsrBackend(&diag);
    }
    if (msr_) {
        cpus_ = msr_->cpus();
    }
}

Controller::~Controller() = default;

std::string Controller::backendName() const {
    return msr_ ? msr_->name() : "none";
}

double Controller::readOffset(Plane plane) {
    if (!msr_) throw Error("no MSR backend");
    const int idx = static_cast<int>(plane);
    const auto msr = static_cast<uint32_t>(addrs_.voltageOffsets);

    const int cpu = cpus_.front();
    msr_->write(cpu, msr, packOffsetRead(idx));
    usleep(1000);
    const uint64_t resp = msr_->read(cpu, msr);

    if (resp == 0) return 0.0;
    return unpackOffsetValue(resp);
}

std::array<double, kPlaneCount> Controller::readAllOffsets(bool* ok) {

    std::array<double, kPlaneCount> out{};
    if (!msr_) {
        if (ok) *ok = false;
        return out;
    }

    constexpr int kOffsetReadAttempts = 3;

    bool allOk = true;
    for (int p = 0; p < kPlaneCount; ++p) {
        double first = 0;
        bool consistent = false;
        bool gotAny = false;

        for (int attempt = 0; attempt < kOffsetReadAttempts; ++attempt) {
            double v = 0;
            bool thisOk = false;
            try {
                v = readOffset(static_cast<Plane>(p));
                thisOk = true;
            } catch (const std::exception&) {
                thisOk = false;
            }

            if (!thisOk) {

                flushMailbox();
                continue;
            }

            if (!gotAny) {
                first = v;
                gotAny = true;
                consistent = true;
            } else if (v != first) {

                consistent = false;
                break;
            }
        }

        if (!gotAny) {
            allOk = false;
            out[p] = 0;
        } else if (!consistent) {

            allOk = false;
            out[p] = first;
        } else {
            out[p] = first;
        }
    }

    if (ok) *ok = allOk;
    return out;
}

void Controller::flushMailbox() {
    if (!msr_ || cpus_.empty()) return;

    constexpr int kInvalidPlane = 15;
    const uint64_t pkg = (1ULL << 63)
                         | (static_cast<uint64_t>(kInvalidPlane) << 40)
                         | (1ULL << 36);
    const int cpu = cpus_.front();
    const uint32_t msrAddr = static_cast<uint32_t>(addrs_.voltageOffsets);
    try {
        msr_->write(cpu, msrAddr, pkg);
        (void)msr_->read(cpu, msrAddr);
    } catch (...) {

    }
}

void Controller::setOffset(Plane plane, double mV, bool force) {
    if (!msr_) throw Error("no MSR backend");
    if (mV > 0 && !force) throw Error("positive offset requires --force");

    const int idx = static_cast<int>(plane);
    const auto msr = static_cast<uint32_t>(addrs_.voltageOffsets);
    const int cpu = cpus_.front();

    const int16_t raw = encodeOffsetRaw(mV);
    const uint64_t payload = packOffsetData(raw, idx);
    msr_->write(cpu, msr, payload);
    usleep(1000);

    msr_->write(cpu, msr, packOffsetRead(idx));
    usleep(1000);
    const int16_t back = static_cast<int16_t>(
        (msr_->read(cpu, msr) >> 21) & 0x7FF);
    if (back != raw) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "offset verify failed: wrote raw=%d read raw=%d", raw, back);
        throw Error(buf);
    }
}

void Controller::readTemperature(int* tccOffset, int* tjMax) {
    if (!msr_) throw Error("no MSR backend");
    const uint64_t val = msr_->read(cpus_.front(), static_cast<uint32_t>(addrs_.temp));

    int tj = static_cast<int>((val >> 16) & 0xFF);
    if (tj == 0) tj = 100;
    const int off = static_cast<int>((val >> 24) & 0x7F);

    if (tccOffset) *tccOffset = off;
    if (tjMax) *tjMax = tj;
}

void Controller::setTemperature(int targetC) {
    if (!msr_) throw Error("no MSR backend");
    const uint64_t val = msr_->read(cpus_.front(), static_cast<uint32_t>(addrs_.temp));

    if ((val & (1ULL << 31)) != 0) {
        throw Error("cannot set temperature target: MSR 0x1A2 is locked by BIOS (bit 31 set)");
    }

    int tj = static_cast<int>((val >> 16) & 0xFF);
    if (tj == 0) tj = 100;

    const int offset = tj - targetC;
    if (offset < 0) {
        throw Error("target temperature (" + std::to_string(targetC) +
                    "°C) cannot exceed CPU TjMax (" + std::to_string(tj) + "°C)");
    }
    if (offset > 63) {
        throw Error("target temperature (" + std::to_string(targetC) +
                    ") offset out of bounds");
    }

    const uint64_t mask = 0x7FULL << 24;
    const uint64_t writeVal = (val & ~mask) | (static_cast<uint64_t>(offset) << 24);

    for (int cpu : cpus_) {
        msr_->write(cpu, static_cast<uint32_t>(addrs_.temp), writeVal);
    }

    const uint64_t back = msr_->read(cpus_.front(), static_cast<uint32_t>(addrs_.temp));
    const int readOffset = static_cast<int>((back >> 24) & 0x7F);
    if (readOffset != offset) {
        throw Error("failed to apply temperature target: set offset " +
                    std::to_string(offset) + "°C, read offset " +
                    std::to_string(readOffset) + "°C");
    }
}

PowerLimit Controller::readPowerLimit() {
    PowerLimit pl;

    auto ltW = readRaplLimit(kRaplLong);
    auto ltT = readRaplWindow(kRaplLong);
    auto stW = readRaplLimit(kRaplShort);
    auto stT = readRaplWindow(kRaplShort);
    if (ltW && ltT) {
        pl.longPower = static_cast<double>(*ltW) / 1e6;
        pl.longTime = static_cast<double>(*ltT) / 1e6;
        pl.longEnabled = true;
        if (stW && stT) {
            pl.shortPower = static_cast<double>(*stW) / 1e6;
            pl.shortTime = static_cast<double>(*stT) / 1e6;
            pl.shortEnabled = true;
        }
        return pl;
    }

    if (!msr_) throw Error("no MSR backend");
    const uint64_t units = msr_->read(cpus_.front(), static_cast<uint32_t>(addrs_.units));
    const uint64_t val =
        msr_->read(cpus_.front(), static_cast<uint32_t>(addrs_.powerLimits));

    const double powerUnit = std::pow(2.0, static_cast<double>(units & 0xF));
    const double timeUnit = std::pow(2.0, static_cast<double>((units >> 16) & 0xF));

    pl.shortEnabled = ((val >> 47) & 0x1) != 0;
    pl.shortPower = static_cast<double>((val >> 32) & 0x7FFF) / powerUnit;
    pl.shortTime = toSeconds(val >> 49, timeUnit);
    pl.longEnabled = ((val >> 15) & 0x1) != 0;
    pl.longPower = static_cast<double>(val & 0x7FFF) / powerUnit;
    pl.longTime = toSeconds(val >> 17, timeUnit);
    pl.locked = ((val >> 63) & 1) != 0;
    pl.backupRest = val & 0x7F010000FF010000ULL;
    return pl;
}

void Controller::setPowerLimit(const PowerLimit& pl) {

    bool raplOk = false;
    if (pl.longPower > 0) {
        const uint64_t uw = static_cast<uint64_t>(pl.longPower * 1e6);
        if (writeRaplLimit(kRaplLong, uw)) {
            raplOk = true;
            if (pl.longTime > 0) {
                writeRaplWindow(kRaplLong, static_cast<uint64_t>(pl.longTime * 1e6));
            }
        }
    }
    if (pl.shortPower > 0) {
        const uint64_t uw = static_cast<uint64_t>(pl.shortPower * 1e6);
        if (writeRaplLimit(kRaplShort, uw)) {
            raplOk = true;
            if (pl.shortTime > 0) {
                writeRaplWindow(kRaplShort, static_cast<uint64_t>(pl.shortTime * 1e6));
            }
        }
    }
    if (raplOk) return;

    if (!msr_) throw Error("no MSR backend and RAPL sysfs unavailable");
    const PowerLimit old = readPowerLimit();
    if (old.locked) throw Error("cannot write power limit because it is locked");

    const uint64_t units = msr_->read(cpus_.front(), static_cast<uint32_t>(addrs_.units));
    const double powerUnit = std::pow(2.0, static_cast<double>(units & 0xF));
    const double timeUnit = std::pow(2.0, static_cast<double>((units >> 16) & 0xF));

    uint64_t writeValue = old.backupRest;

    bool stEnabled = old.shortEnabled;
    double stPower = old.shortPower;
    double stTime = old.shortTime;
    if (pl.shortPower > 0) {
        stEnabled = true;
        stPower = pl.shortPower;
        stTime = pl.shortTime;
    }
    if (stEnabled) writeValue |= (1ULL << 47);
    const int stPowerVal = static_cast<int>(stPower * powerUnit);
    if (stPowerVal < 0 || stPowerVal > 0x7FFF) throw Error("short term power out of range");
    writeValue |= static_cast<uint64_t>(stPowerVal) << 32;
    writeValue |= fromSeconds(stTime, timeUnit) << 49;

    bool ltEnabled = old.longEnabled;
    double ltPower = old.longPower;
    double ltTime = old.longTime;
    if (pl.longPower > 0) {
        ltEnabled = true;
        ltPower = pl.longPower;
        ltTime = pl.longTime;
    }
    if (ltEnabled) writeValue |= (1ULL << 15);
    const int ltPowerVal = static_cast<int>(ltPower * powerUnit);
    if (ltPowerVal < 0 || ltPowerVal > 0x7FFF) throw Error("long term power out of range");
    writeValue |= static_cast<uint64_t>(ltPowerVal);
    writeValue |= fromSeconds(ltTime, timeUnit) << 17;

    if (old.locked || pl.locked) writeValue |= (1ULL << 63);

    for (int cpu : cpus_) {
        msr_->write(cpu, static_cast<uint32_t>(addrs_.powerLimits), writeValue);
    }
}

std::optional<bool> Controller::readTurbo() {
    auto v = readFile(kNoTurboPath);
    if (!v) return std::nullopt;
    return *v == "0";
}

void Controller::setTurbo(bool enabled) {
    const std::string val = enabled ? "0" : "1";
    if (!writeFile(kNoTurboPath, val)) {
        throw Error(std::string("failed to write ") + kNoTurboPath +
                    " (need root, and intel_pstate must be active)");
    }
}

Sample Controller::sample() {
    Sample s;

    s.planes = readAllOffsets(&s.planesOk);

    try {
        readTemperature(&s.tccOffset, &s.tjMax);
        s.tempOk = true;
    } catch (...) {
        s.tempOk = false;
    }

    try {
        s.powerLimit = readPowerLimit();
        s.powerLimitOk = true;
    } catch (...) {
        s.powerLimitOk = false;
    }

    if (auto t = readTurbo()) {
        s.turboOn = *t;
        s.turboOk = true;
    }

    const TempReading tr = readHwmonTemps();
    s.packageTemp = tr.package;
    s.coreMaxTemp = tr.coreMax;
    s.tempSensorOk = tr.ok;

    if (auto fan = readFanRpm()) {
        s.fanRpm = *fan;
        s.fanOk = true;
    }
    if (auto f = readCpuFreq()) {
        s.freqMhz = *f;
        s.freqOk = true;
    }

    if (auto energy = readRaplEnergy()) {
        const auto now = std::chrono::steady_clock::now();
        if (raplPrimed_ && *energy >= static_cast<uint64_t>(lastRaplEnergy_)) {
            const double dt =
                std::chrono::duration<double>(now - lastRaplTime_).count();
            if (dt > 0) {
                s.powerW = (static_cast<double>(*energy) - lastRaplEnergy_) / 1e6 / dt;
                s.powerOk = true;
            }
        }
        lastRaplEnergy_ = static_cast<double>(*energy);
        lastRaplTime_ = now;
        raplPrimed_ = true;
    }

    return s;
}

}
