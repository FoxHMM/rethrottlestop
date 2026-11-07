

#pragma once

#include <string>
#include <vector>

namespace uv {

constexpr const char* kConfigDir = "/etc/undervolt-go";
constexpr const char* kConfigFile = "/etc/undervolt-go/config.yaml";

struct ProfileSettings {
    double core = 0;
    double cache = 0;
    double gpu = 0;
    double uncore = 0;
    double analogio = 0;
    int temp = 0;
    int turbo = -1;
    double p1Power = 0;
    double p1Time = 0;
    double p2Power = 0;
    double p2Time = 0;
};

bool loadProfile(const std::string& name, ProfileSettings* out);

bool saveProfile(const std::string& name, const ProfileSettings& s);

std::vector<std::string> listProfiles();

void enablePersistence();
void disablePersistence();

int runProfileCommand(int argc, char** argv);

}
