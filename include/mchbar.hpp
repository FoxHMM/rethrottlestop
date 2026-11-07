
#ifndef UV_MCHBAR_HPP
#define UV_MCHBAR_HPP

#include <cstdint>
#include <optional>

namespace uv {

class Mchbar {
  public:
    Mchbar();
    ~Mchbar();
    Mchbar(const Mchbar&) = delete;
    Mchbar& operator=(const Mchbar&) = delete;

    bool available() const;
    uint32_t base() const;

    std::optional<double> readVccin() const;

    std::optional<double> readMemDdr() const;

    uint32_t read32(uint32_t offset) const;
    uint32_t readRaw(uint32_t offset) const;

  private:
    struct Impl;
    Impl* impl_;
};

}
#endif
