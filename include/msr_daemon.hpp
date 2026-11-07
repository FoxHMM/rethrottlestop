

#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace uv {

class MsrClient {
  public:

    static bool ensureDaemon();

    static std::optional<double> readOffset(int plane);

    static bool writeOffset(int plane, double mV);

    static std::optional<double> readIccMax(int domain);

    static bool writeIccMax(int domain, double amps);

    static std::optional<int> readCacheRatio(int domain);

    static std::optional<uint64_t> readMsr(uint32_t msr);
    static std::optional<uint64_t> readMsrOn(int cpu, uint32_t msr);
    static bool writeMsr(uint32_t msr, uint64_t value);
    static bool writeMsrOn(int cpu, uint32_t msr, uint64_t value);
    static bool writeMsrAllCores(uint32_t msr, uint64_t value);

    static std::string daemonStatus();
};

int msr_daemon_run(int intervalMs);

}
