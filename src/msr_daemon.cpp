

#include "../include/msr_daemon.hpp"
#include "../include/undervolt.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <csignal>
#include <atomic>
#include <poll.h>

namespace uv {

namespace {

std::string resolveSocketPath() {

    if (::access("/run/rethrottlestop", W_OK) == 0) {
        return "/run/rethrottlestop/msr.sock";
    }

    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR")) {
        std::string dir = std::string(xdg) + "/rethrottlestop";
        ::mkdir(dir.c_str(), 0700);
        if (::access(dir.c_str(), W_OK) == 0) {
            return dir + "/msr.sock";
        }
    }

    return "/tmp/rethrottlestop-msr.sock";
}

const char* socketPath() {
    static const std::string p = resolveSocketPath();
    return p.c_str();
}

int g_msrFd = -1;

std::atomic<bool> g_reloadConfig{false};

extern "C" void sigusr1Handler(int) {
    g_reloadConfig.store(true);
}

void replayConfig() {

    std::fprintf(stderr, "msr-daemon: 周期重放（interval 到点）\n");
}

bool openMsr() {
    if (g_msrFd >= 0) return true;
    g_msrFd = ::open("/dev/cpu/0/msr", O_RDWR);
    return g_msrFd >= 0;
}

uint64_t readMsrRaw(uint32_t msr) {
    uint64_t v = 0;
    if (g_msrFd < 0) return 0;
    ::pread(g_msrFd, &v, 8, msr);
    return v;
}

bool writeMsrRawOn(int cpu, uint32_t msr, uint64_t v) {
    char path[64];
    std::snprintf(path, sizeof(path), "/dev/cpu/%d/msr", cpu);
    const int fd = ::open(path, O_RDWR);
    if (fd < 0) return false;
    const bool ok = (::pwrite(fd, &v, 8, msr) == 8);
    ::close(fd);
    return ok;
}

uint64_t readMsrRawOn(int cpu, uint32_t msr) {
    if (cpu <= 0) return readMsrRaw(msr);
    char path[64];
    std::snprintf(path, sizeof(path), "/dev/cpu/%d/msr", cpu);
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) return 0;
    uint64_t v = 0;
    ::pread(fd, &v, 8, msr);
    ::close(fd);
    return v;
}

bool writeMsrRaw(uint32_t msr, uint64_t v) {
    if (g_msrFd < 0) return false;
    return ::pwrite(g_msrFd, &v, 8, msr) == 8;
}

std::string handleRequest(const std::string& line) {
    if (line.empty()) return "ERR empty";

    char cmd = line[0];
    switch (cmd) {
        case 'R': {
            int plane = 0;
            if (std::sscanf(line.c_str(), "R %d", &plane) != 1) return "ERR bad arg";
            if (plane < 0 || plane > 4) return "ERR plane range";
            const uint64_t cmdv = (0x80000010ULL | ((uint64_t)plane << 8)) << 32;
            writeMsrRaw(0x150, cmdv);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const uint64_t resp = readMsrRaw(0x150);
            if (resp == 0) { char b[32]; std::snprintf(b, sizeof(b), "V 0.0"); return b; }
            const int16_t raw = (int16_t)((resp >> 21) & 0x7FF);

            double mv;
            if (raw == 0 || raw == 2047) mv = 0.0;
            else mv = -(2048.0 - static_cast<double>(raw)) / 1.024;
            char b[64]; std::snprintf(b, sizeof(b), "V %.4f", mv); return b;
        }
        case 'W': {
            int plane = 0; double mv = 0;
            if (std::sscanf(line.c_str(), "W %d %lf", &plane, &mv) != 2) return "ERR bad arg";
            if (plane < 0 || plane > 4) return "ERR plane range";
            if (mv > 0) return "ERR positive";

            int raw = (int)(2048.0 - std::fabs(mv) * 1.024);
            if (raw < 0) raw = 0;
            if (raw > 2047) raw = 2047;
            const uint64_t v = 0x8000001000000000ULL
                             | ((uint64_t)plane << 40)
                             | ((uint64_t)raw << 21)
                             | (1ULL << 32);
            if (!writeMsrRaw(0x150, v)) return "ERR write";
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return "OK";
        }
        case 'I': {
            int d = 0;
            if (std::sscanf(line.c_str(), "I %d", &d) != 1) return "ERR bad arg";
            const uint64_t cmdv = (0x80000010ULL | ((uint64_t)d << 8)) << 32;
            writeMsrRaw(0x150, cmdv);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const uint64_t resp = readMsrRaw(0x150);
            if ((resp & 0x1F) != 0) return "ERR rc";
            const uint32_t raw = (uint32_t)(resp & 0xFF);
            if (raw == 0) return "ERR nodata";
            char b[64]; std::snprintf(b, sizeof(b), "V %.2f", raw * 0.25); return b;
        }
        case 'C': {
            int d = 0;
            if (std::sscanf(line.c_str(), "C %d", &d) != 1) return "ERR bad arg";
            const uint64_t cmdv = (0x80000018ULL | ((uint64_t)d << 8)) << 32;
            writeMsrRaw(0x150, cmdv);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const uint64_t resp = readMsrRaw(0x150);
            if ((resp & 0x1F) != 0) return "ERR rc";
            char b[32]; std::snprintf(b, sizeof(b), "V %u", (unsigned)(resp & 0xF)); return b;
        }
        case 'M': {
            unsigned msr = 0; int cpu = 0;
            const int n = std::sscanf(line.c_str(), "M %x %d", &msr, &cpu);
            if (n < 1) return "ERR bad arg";
            char b[64]; std::snprintf(b, sizeof(b), "V 0x%016llX",
                (unsigned long long)readMsrRawOn(cpu, msr)); return b;
        }
        case 'N': {
            unsigned msr = 0; unsigned long long v = 0; int cpu = 0;
            const int n = std::sscanf(line.c_str(), "N %x %llx %d", &msr, &v, &cpu);
            if (n < 2) return "ERR bad arg";

            if (cpu < 0) {
                const int ncpu = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
                int ok = 0;
                for (int c = 0; c < ncpu; ++c) {
                    if (writeMsrRawOn(c, msr, v)) ++ok;
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                char b[32];
                std::snprintf(b, sizeof(b), "OK %d/%d", ok, ncpu);
                return b;
            }

            return writeMsrRawOn(cpu, msr, v) ? "OK" : "ERR write";
        }
        case 'S': {
            char b[128];
            std::snprintf(b, sizeof(b), "V msr_fd=%d", g_msrFd);
            return b;
        }
        default:
            return "ERR unknown";
    }
}

}

int msr_daemon_run(int intervalMs) {
        if (!openMsr()) {
        std::fprintf(stderr, "msr-daemon: 无法打开 /dev/cpu/0/msr\n");
        return 1;
    }

    ::unlink(socketPath());

    const int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { std::perror("socket"); return 1; }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath(), sizeof(addr.sun_path) - 1);

    if (::bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("bind"); ::close(srv); return 1;
    }
    ::chmod(socketPath(), 0666);
    ::listen(srv, 8);

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1Handler;
    ::sigaction(SIGUSR1, &sa, nullptr);

    std::fprintf(stderr, "msr-daemon: 监听 %s, interval=%dms\n",
                 socketPath(), intervalMs);

    int64_t lastReplay = 0;

    for (;;) {

        if (g_reloadConfig.exchange(false)) {
            std::fprintf(stderr, "msr-daemon: 收到 SIGUSR1，重载配置\n");
            replayConfig();
            lastReplay = 0;
        }

        const auto now = std::chrono::steady_clock::now();
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        if (lastReplay == 0) lastReplay = nowMs;
        if (nowMs - lastReplay >= intervalMs) {
            replayConfig();
            lastReplay = nowMs;
        }

        struct pollfd pfd;
        pfd.fd = srv;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, 100);
        if (pr <= 0) continue;

        const int cli = ::accept(srv, nullptr, nullptr);
        if (cli < 0) continue;

        char buf[256];
        const ssize_t n = ::read(cli, buf, sizeof(buf) - 1);
        std::string resp = "ERR empty";
        if (n > 0) {
            buf[n] = '\0';
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            resp = handleRequest(line);
        }
        ::write(cli, resp.c_str(), resp.size());
        ::close(cli);
    }
    return 0;
}

namespace {

std::string findActiveSocket() {
    std::vector<std::string> candidates;

    candidates.push_back("/run/rethrottlestop/msr.sock");

    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR")) {
        candidates.push_back(std::string(xdg) + "/rethrottlestop/msr.sock");
    }

    candidates.push_back("/tmp/rethrottlestop-msr.sock");

    for (const auto& path : candidates) {
        if (::access(path.c_str(), F_OK) != 0) continue;

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) continue;

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            ::close(fd);
            return path;
        }
        ::close(fd);
    }
    return {};
}

std::string sendRequest(const std::string& req) {
    const std::string path = findActiveSocket();
    if (path.empty()) return "";

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "";

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return "";
    }
    ::write(fd, req.c_str(), req.size());
    char buf[256] = {0};
    const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    return (n > 0) ? std::string(buf, n) : std::string();
}

}

bool MsrClient::ensureDaemon() {
    if (!sendRequest("S").empty()) return true;

    const int rc = std::system(
        "systemctl start rethrottlestop-daemon 2>/dev/null || "
        "(setsid /usr/bin/rethrottlestop --daemon >/dev/null 2>&1 &)");
    (void)rc;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return !sendRequest("S").empty();
}

std::optional<double> MsrClient::readOffset(int plane) {
    const std::string r = sendRequest("R " + std::to_string(plane));
    if (r.rfind("V ", 0) != 0) return std::nullopt;
    return std::atof(r.c_str() + 2);
}

bool MsrClient::writeOffset(int plane, double mV) {
    char b[64];
    std::snprintf(b, sizeof(b), "W %d %.4f", plane, mV);
    return sendRequest(b) == "OK";
}

std::optional<double> MsrClient::readIccMax(int domain) {
    const std::string r = sendRequest("I " + std::to_string(domain));
    if (r.rfind("V ", 0) != 0) return std::nullopt;
    return std::atof(r.c_str() + 2);
}

bool MsrClient::writeIccMax(int domain, double amps) {
    char b[64];
    std::snprintf(b, sizeof(b), "J %d %.2f", domain, amps);
    return sendRequest(b) == "OK";
}

std::optional<int> MsrClient::readCacheRatio(int domain) {
    const std::string r = sendRequest("C " + std::to_string(domain));
    if (r.rfind("V ", 0) != 0) return std::nullopt;
    return std::atoi(r.c_str() + 2);
}

std::optional<uint64_t> MsrClient::readMsr(uint32_t msr) {
    return readMsrOn(0, msr);
}

std::optional<uint64_t> MsrClient::readMsrOn(int cpu, uint32_t msr) {
    char b[48];
    std::snprintf(b, sizeof(b), "M %x %d", msr, cpu);
    const std::string r = sendRequest(b);
    if (r.rfind("V ", 0) != 0) return std::nullopt;
    return std::strtoull(r.c_str() + 2, nullptr, 16);
}

bool MsrClient::writeMsr(uint32_t msr, uint64_t value) {
    return writeMsrOn(0, msr, value);
}

bool MsrClient::writeMsrOn(int cpu, uint32_t msr, uint64_t value) {
    char b[64];
    std::snprintf(b, sizeof(b), "N %x %llx %d", msr,
                  (unsigned long long)value, cpu);
    const std::string r = sendRequest(b);
    return r.rfind("OK", 0) == 0;
}

bool MsrClient::writeMsrAllCores(uint32_t msr, uint64_t value) {
    char b[64];
    std::snprintf(b, sizeof(b), "N %x %llx -1", msr,
                  (unsigned long long)value);
    const std::string r = sendRequest(b);
    return r.rfind("OK", 0) == 0;
}

std::string MsrClient::daemonStatus() {
    const std::string r = sendRequest("S");
    return r.empty() ? "未运行" : r.substr(2);
}

}

extern "C" int msr_daemon_run(int intervalMs) {
    return uv::msr_daemon_run(intervalMs);
}

namespace uv {

class DaemonMsrBackend : public MsrBackend {
  public:
    explicit DaemonMsrBackend(std::vector<int> cpus) : cpus_(std::move(cpus)) {}

    uint64_t read(int cpu, uint32_t msr) override {
        auto v = MsrClient::readMsrOn(cpu, msr);
        return v ? *v : 0;
    }

    void write(int cpu, uint32_t msr, uint64_t value) override {

        if (!MsrClient::writeMsrOn(cpu, msr, value)) {
            throw std::runtime_error("daemon write rejected");
        }
    }

    void batch(std::vector<MsrBatchOp>& ops) override {
        for (auto& op : ops) {
            try {
                if (op.isrdmsr) {
                    op.msrdata = read(op.cpu, op.msr);
                } else {
                    write(op.cpu, op.msr, op.msrdata);
                }
                op.err = 0;
            } catch (...) {
                op.err = -1;
            }
        }
    }

    std::string name() const override { return "daemon"; }
    bool supportsBatch() const override { return false; }
    std::vector<int> cpus() override { return cpus_; }

  private:
    std::vector<int> cpus_;
};

std::unique_ptr<MsrBackend> createDaemonBackend(std::string* diag) {
    if (!MsrClient::ensureDaemon()) {
        if (diag) *diag = "daemon unavailable";
        return nullptr;
    }
    if (!MsrClient::readMsr(0x198)) {
        if (diag) *diag = "daemon not responding";
        return nullptr;
    }
    std::vector<int> cpus;
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    for (long i = 0; i < n; ++i) cpus.push_back(static_cast<int>(i));
    if (diag) *diag = "daemon ok, " + std::to_string(cpus.size()) + " cpus";
    return std::make_unique<DaemonMsrBackend>(std::move(cpus));
}

}
