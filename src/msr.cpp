

#include "undervolt.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace uv {

namespace {

#ifndef X86_IOC_RDMSR_REGS
#define X86_IOC_RDMSR_REGS _IOWR('c', 0xA0, __u32[8])
#endif
#ifndef X86_IOC_WRMSR_REGS
#define X86_IOC_WRMSR_REGS _IOWR('c', 0xA1, __u32[8])
#endif

struct msr_batch_op {
    uint16_t cpu;
    uint16_t isrdmsr;
    int32_t err;
    uint32_t msr;
    uint64_t msrdata;
    uint64_t wmask;
};

struct msr_batch_array {
    uint32_t numops;
    uint32_t version;
    struct msr_batch_op* ops;
};

#ifndef X86_IOC_MSR_BATCH
#define X86_IOC_MSR_BATCH _IOWR('c', 0xA2, struct msr_batch_array)
#endif

constexpr uint32_t kMsrSafeVersion = 1;

std::mutex g_msrMutex;

std::vector<int> probeCpus() {
    std::vector<int> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/dev/cpu", ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        try {
            int idx = std::stoi(name);
            out.push_back(idx);
        } catch (...) {
            continue;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

class MsrSafeBackend final : public MsrBackend {
  public:
    MsrSafeBackend() = default;

    bool open(std::string* diag) {
        cpus_ = probeCpus();
        if (cpus_.empty()) {
            if (diag) *diag = "no /dev/cpu/* directories";
            return false;
        }

        for (int cpu : cpus_) {
            const std::string path = "/dev/cpu/" + std::to_string(cpu) + "/msr_safe";
            int fd = ::open(path.c_str(), O_RDWR);
            if (fd < 0) {
                if (diag) *diag = "cannot open " + path + ": " + std::strerror(errno);
                return false;
            }
            fds_[cpu] = fd;
        }
        return true;
    }

    ~MsrSafeBackend() override {
        for (auto& [cpu, fd] : fds_) {
            if (fd >= 0) ::close(fd);
        }
    }

    uint64_t read(int cpu, uint32_t msr) override {
        std::vector<MsrBatchOp> ops(1);
        ops[0].cpu = static_cast<uint16_t>(cpu);
        ops[0].isrdmsr = 1;
        ops[0].msr = msr;
        batch(ops);
        if (ops[0].err != 0) {
            throw Error("msr-safe read 0x" + toHex(msr) + " failed: " +
                        std::strerror(ops[0].err));
        }
        return ops[0].msrdata;
    }

    void write(int cpu, uint32_t msr, uint64_t value) override {
        std::vector<MsrBatchOp> ops(1);
        ops[0].cpu = static_cast<uint16_t>(cpu);
        ops[0].isrdmsr = 0;
        ops[0].msr = msr;
        ops[0].msrdata = value;
        batch(ops);
        if (ops[0].err != 0) {
            throw Error("msr-safe write 0x" + toHex(msr) + " failed: " +
                        std::strerror(ops[0].err));
        }
    }

    void batch(std::vector<MsrBatchOp>& ops) override {
        std::lock_guard<std::mutex> lock(g_msrMutex);
        if (ops.empty()) return;

        std::vector<msr_batch_op> raw(ops.size());
        for (size_t i = 0; i < ops.size(); ++i) {
            raw[i].cpu = ops[i].cpu;
            raw[i].isrdmsr = ops[i].isrdmsr;
            raw[i].err = 0;
            raw[i].msr = ops[i].msr;
            raw[i].msrdata = ops[i].msrdata;
            raw[i].wmask = ops[i].wmask;
        }
        msr_batch_array arr{};
        arr.numops = static_cast<uint32_t>(raw.size());
        arr.version = kMsrSafeVersion;
        arr.ops = raw.data();

        int fd = fds_.begin()->second;
        if (::ioctl(fd, X86_IOC_MSR_BATCH, &arr) < 0) {

            for (auto& op : ops) op.err = errno;
            return;
        }
        for (size_t i = 0; i < ops.size(); ++i) {
            ops[i].err = raw[i].err;
            ops[i].msrdata = raw[i].msrdata;
        }
    }

    std::string name() const override { return "msr-safe (batch)"; }
    bool supportsBatch() const override { return true; }
    std::vector<int> cpus() override { return cpus_; }

  private:
    static std::string toHex(uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%x", v);
        return buf;
    }

    std::map<int, int> fds_;
    std::vector<int> cpus_;
};

class LegacyMsrBackend final : public MsrBackend {
  public:
    bool open(std::string* diag) {
        cpus_ = probeCpus();
        if (cpus_.empty()) {
            if (diag) *diag = "no /dev/cpu/* directories";
            return false;
        }

        bool any = false;
        bool readOnlyOnly = true;
        for (int cpu : cpus_) {
            const std::string path = "/dev/cpu/" + std::to_string(cpu) + "/msr";
            int fd = ::open(path.c_str(), O_RDWR);
            if (fd < 0) {

                fd = ::open(path.c_str(), O_RDONLY);
                if (fd >= 0) {
                    fds_[cpu] = fd;
                    any = true;
                }
                continue;
            }
            fds_[cpu] = fd;
            any = true;
            readOnlyOnly = false;
        }
        if (!any) {
            if (diag) {
                *diag = "cannot open /dev/cpu/*/msr: " + std::string(std::strerror(errno)) +
                        " (kernel msr driver requires CAP_SYS_RAWIO; "
                        "run as root or setcap cap_sys_rawio+ep on this binary)";
            }
            return false;
        }
        readOnly_ = readOnlyOnly;
        return true;
    }

    ~LegacyMsrBackend() override {
        for (auto& [cpu, fd] : fds_) {
            if (fd >= 0) ::close(fd);
        }
    }

    uint64_t read(int cpu, uint32_t msr) override {
        std::lock_guard<std::mutex> lock(g_msrMutex);
        int fd = fdFor(cpu);
        uint64_t val = 0;
        if (::pread(fd, &val, sizeof(val), msr) != static_cast<ssize_t>(sizeof(val))) {
            throw Error("pread msr 0x" + toHex(msr) + " failed: " + std::strerror(errno));
        }
        return val;
    }

    void write(int cpu, uint32_t msr, uint64_t value) override {
        std::lock_guard<std::mutex> lock(g_msrMutex);
        int fd = fdFor(cpu);
        if (::pwrite(fd, &value, sizeof(value), msr) != static_cast<ssize_t>(sizeof(value))) {
            throw Error("pwrite msr 0x" + toHex(msr) + " failed: " + std::strerror(errno));
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
            } catch (const std::exception&) {
                op.err = EIO;
            }
        }
    }

    std::string name() const override { return "legacy msr (per-call)"; }
    std::vector<int> cpus() override { return cpus_; }

  private:
    static std::string toHex(uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%x", v);
        return buf;
    }

    int fdFor(int cpu) {
        auto it = fds_.find(cpu);
        if (it != fds_.end()) return it->second;

        const std::string path = "/dev/cpu/" + std::to_string(cpu) + "/msr";
        int fd = ::open(path.c_str(), O_RDWR);
        if (fd < 0) {
            throw Error("cannot open " + path + ": " + std::strerror(errno));
        }
        fds_[cpu] = fd;
        return fd;
    }

    std::map<int, int> fds_;
    std::vector<int> cpus_;
    bool readOnly_ = false;
};

}

std::unique_ptr<MsrBackend> createMsrBackend(std::string* diag) {
    std::string safeDiag, legacyDiag;

    {
        auto backend = std::make_unique<MsrSafeBackend>();
        if (backend->open(&safeDiag)) {
            return backend;
        }
    }

    {
        auto backend = std::make_unique<LegacyMsrBackend>();
        if (backend->open(&legacyDiag)) {
            if (diag) {
                *diag = "msr-safe unavailable (" + safeDiag + "), using legacy msr";
            }
            return backend;
        }
    }

    if (diag) {
        *diag = "no MSR backend available. msr-safe: " + safeDiag +
                "; legacy: " + legacyDiag;
    }
    return nullptr;
}

bool hasMsrAccess() {
    std::string diag;
    auto backend = createMsrBackend(&diag);
    return backend != nullptr;
}

}
