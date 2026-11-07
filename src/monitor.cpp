

#include "undervolt.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace uv {

namespace {

std::optional<std::string> readFirstLine(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::string line;
    if (!std::getline(f, line)) return std::nullopt;
    const auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::nullopt;
    const auto e = line.find_last_not_of(" \t\r\n");
    return line.substr(b, e - b + 1);
}

std::vector<std::filesystem::path> glob(const std::string& dir, const std::string& prefix) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return out;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string name = e.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}

std::optional<uint64_t> readRaplEnergy(int domain) {
    char path[256];
    std::snprintf(path, sizeof(path),
                  "/sys/class/powercap/intel-rapl:%d/energy_uj", domain);
    auto v = readFirstLine(path);
    if (!v) return std::nullopt;
    try {
        return std::stoull(*v);
    } catch (...) {
        return std::nullopt;
    }
}

TempReading readHwmonTemps() {
    TempReading out;
    const std::string base = "/sys/class/hwmon";

    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) return out;

    bool any = false;
    double maxAll = 0;

    for (const auto& hw : std::filesystem::directory_iterator(base, ec)) {
        if (ec) break;
        if (!hw.is_directory()) continue;

        auto nameOpt = readFirstLine((hw.path() / "name").string());
        if (!nameOpt) continue;
        const std::string& chip = *nameOpt;
        if (chip != "coretemp" && chip != "k10temp" && chip != "zenpower" &&
            chip != "cpu_thermal") {
            continue;
        }

        for (const auto& p : glob(hw.path().string(), "temp")) {
            const std::string fname = p.filename().string();
            if (fname.size() < 11 ||
                fname.substr(fname.size() - 6) != "_input") {
                continue;
            }
            auto valOpt = readFirstLine(p.string());
            if (!valOpt) continue;

            double milli = 0;
            try {
                milli = std::stod(*valOpt);
            } catch (...) {
                continue;
            }
            const double celsius = milli / 1000.0;

            std::string labelPath = p.string();
            labelPath.replace(labelPath.size() - 6, 6, "_label");
            std::string label;
            if (auto l = readFirstLine(labelPath)) label = *l;

            any = true;
            maxAll = std::max(maxAll, celsius);

            if (label.find("Package") != std::string::npos) {
                out.package = celsius;
            } else if (label.find("Core") != std::string::npos) {
                out.coreMax = std::max(out.coreMax, celsius);
            }
        }
    }

    if (!any) return out;

    out.ok = true;

    if (out.package <= 0) out.package = maxAll;
    if (out.coreMax <= 0) out.coreMax = maxAll;
    return out;
}

std::optional<double> readFanRpm() {
    const std::string base = "/sys/class/hwmon";
    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) return std::nullopt;

    double best = 0;
    bool found = false;

    for (const auto& hw : std::filesystem::directory_iterator(base, ec)) {
        if (ec) break;
        if (!hw.is_directory()) continue;

        for (const auto& p : glob(hw.path().string(), "fan")) {
            const std::string fname = p.filename().string();
            if (fname.size() < 11 || fname.substr(fname.size() - 6) != "_input") {
                continue;
            }
            auto v = readFirstLine(p.string());
            if (!v) continue;
            double rpm = 0;
            try {
                rpm = std::stod(*v);
            } catch (...) {
                continue;
            }

            if (rpm > 0) {
                best = std::max(best, rpm);
                found = true;
            }
        }
    }

    if (!found) return std::nullopt;
    return best;
}

std::optional<double> readCpuFreq() {

    std::ifstream f("/proc/cpuinfo");
    if (!f) return std::nullopt;

    double sum = 0;
    int n = 0;
    std::string line;
    while (std::getline(f, line)) {
        const auto pos = line.find("cpu MHz");
        if (pos == std::string::npos) continue;
        const auto colon = line.find(':', pos);
        if (colon == std::string::npos) continue;
        try {
            sum += std::stod(line.substr(colon + 1));
            n++;
        } catch (...) {}
    }
    if (n == 0) return std::nullopt;
    return sum / n;
}

std::string cpuModelName() {
    std::ifstream f("/proc/cpuinfo");
    if (!f) return "N/A";
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                const auto b = line.find_first_not_of(" \t", colon + 1);
                if (b != std::string::npos) return line.substr(b);
            }
        }
    }
    return "N/A";
}

int cpuThreadCount() {
    std::ifstream f("/proc/cpuinfo");
    if (!f) return 0;
    int n = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("processor", 0) == 0) n++;
    }
    return n;
}

bool isBatteryDischarging() {
    const std::string base = "/sys/class/power_supply";
    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) return false;

    for (const auto& e : std::filesystem::directory_iterator(base, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;

        auto type = readFirstLine((e.path() / "type").string());
        if (!type || type->rfind("Battery", 0) != 0) continue;

        auto status = readFirstLine((e.path() / "status").string());
        if (status && status->rfind("Discharging", 0) == 0) {
            return true;
        }
    }
    return false;
}

}
