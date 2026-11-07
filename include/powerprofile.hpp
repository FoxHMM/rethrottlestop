
#ifndef UV_POWERPROFILE_HPP
#define UV_POWERPROFILE_HPP

#include <optional>
#include <string>

namespace uv {

class PowerProfile {
  public:
    enum class Level { Performance, Balanced, PowerSaver };

    struct State {
        Level level = Level::Balanced;
        bool ok = false;
        std::string source;
    };

    static State current();
    static bool set(Level level);
    static const char* levelName(Level level);
};

}
#endif
