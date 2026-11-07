

#include "../include/mchbar.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

namespace uv {

namespace {

constexpr uint32_t kMchbarSize = 0x20000;

uint32_t locateMchbarViaPci() {
    const char* path = "/sys/bus/pci/devices/0000:00:00.0/config";
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return 0;

    uint32_t raw = 0;
    const ssize_t n = ::pread(fd, &raw, 4, 0x48);
    ::close(fd);
    if (n != 4) return 0;

    if ((raw & 1) == 0) return 0;
    return raw & 0xFFFFC000u;
}

}

struct Mchbar::Impl {
    int fd = -1;
    void* map = nullptr;
    uint32_t base = 0;
    bool ok = false;

    ~Impl() {
        if (map && map != MAP_FAILED) {
            ::munmap(map, kMchbarSize);
        }
        if (fd >= 0) ::close(fd);
    }
};

Mchbar::Mchbar() : impl_(new Impl()) {
    impl_->base = locateMchbarViaPci();
    if (impl_->base == 0) return;

    impl_->fd = ::open("/dev/mem", O_RDONLY | O_SYNC);
    if (impl_->fd < 0) return;

    impl_->map = ::mmap(nullptr, kMchbarSize, PROT_READ, MAP_SHARED,
                        impl_->fd, impl_->base);
    if (impl_->map == MAP_FAILED) {
        impl_->map = nullptr;
        return;
    }

    const uint32_t probe = *reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(impl_->map));
    impl_->ok = (probe != 0) && (probe != 0xFFFFFFFF);

    if (impl_->ok) {
        const uint32_t vccin = read32(0x59A0);
        if (vccin == 0 || vccin == 0xFFFFFFFF) impl_->ok = false;
    }
}

Mchbar::~Mchbar() { delete impl_; }

bool Mchbar::available() const { return impl_ && impl_->ok; }

uint32_t Mchbar::base() const { return impl_ ? impl_->base : 0; }

uint32_t Mchbar::read32(uint32_t offset) const {
    if (!impl_ || !impl_->ok || offset + 4 > kMchbarSize) return 0;
    return *reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(impl_->map) + offset);
}

namespace {

std::optional<double> readCachedValue(const char* key) {
    FILE* f = std::fopen("/run/rethrottlestop/mchbar.txt", "r");
    if (!f) return std::nullopt;

    char line[128];
    std::optional<double> result;
    const size_t keyLen = std::strlen(key);
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, key, keyLen) == 0 && line[keyLen] == '=') {
            const char* val = line + keyLen + 1;
            if (val[0] == 'n') break;
            try { result = std::stod(val); } catch (...) {}
            break;
        }
    }
    std::fclose(f);
    return result;
}
}

std::optional<double> Mchbar::readVccin() const {
    if (!available()) {

        return readCachedValue("vccin");
    }

    const uint32_t v = read32(0x59A0);
    if (v == 0 || v == 0xFFFFFFFF) return std::nullopt;

    const uint32_t idx = v & 0x3FF;
    const double volts = 0.8 + static_cast<double>(idx) * 0.005;

    if (volts < 1.2 || volts > 3.0) return std::nullopt;
    return volts;
}

std::optional<double> Mchbar::readMemDdr() const {
    if (!available()) {
        return readCachedValue("memddr");
    }

    const uint32_t v = read32(0x59B0);

    if (v == 0 || v == 0xFFFFFFFF) return std::nullopt;

    const uint32_t idx = v & 0x3FF;
    const double volts = 1.0 + static_cast<double>(idx) * 0.005;
    if (volts < 0.8 || volts > 2.0) return std::nullopt;
    return volts;
}

uint32_t Mchbar::readRaw(uint32_t offset) const { return read32(offset); }

}
