

#pragma once

#include <string>
#include <array>
#include <cstdint>

namespace uv {

struct FivrSettings {
    int domain = 0;
    double offsets[6] = {0,0,0,0,0,0};
    bool adaptive = true;
    int rangeMv = 125;
    int iccMax = 0;
    int cacheRatio = 0;
    int vccinRange = 0;

    int avxOffset = 0;
    bool tvb = false;
    bool ringDownBin = false;
    bool vMaxStress = false;
    bool mvBoost = false;
    int mvBoostMv = 0;
    bool overclock = false;

    int saveMode = 0;
    bool unlocked = false;
};

struct TurboSettings {
    std::array<int, 8> ratios{};
    int extraVoltage = 0;
    bool ntrLock = false;
    int ntrRatio1 = 0;
    int ntrRatio2 = 0;
    bool unlimited = false;
    int perfCoreType = 0;
};

struct PowerSettings {
    double pl1W = 0;
    double pl2W = 0;
    double pl1WindowUs = 0;
    double pl2WindowUs = 0;
};

struct CStateSettings {
    bool cStateEnable = false;
    bool c1Demotion = false;
    bool c3Demotion = false;
    bool c1Undemotion = false;
    bool c3Undemotion = false;
    int pkgCStateLimit = 0;
};

struct MainWindowSettings {
    bool clockMod = false;
    int clockModPercent = 100;
    bool setMultiplier = false;
    int multiplier = 0;
    bool speedShift = false;
    int epp = 128;
    bool powerSaver = false;
    bool disableTurbo = false;
    bool speedStep = true;
    bool bdProchot = true;
    bool c1e = true;
    bool taskBar = false;
    bool onTop = false;
    bool logFile = false;
    bool moreData = false;
    int powerProfile = 1;
    int activeProfile = 0;

    std::array<std::string, 4> profileName{};
};

struct VfSettings {
    int domain = 0;

    int points[11][4] = {{0}};
};

struct Settings {
    FivrSettings fivr;
    TurboSettings turbo;
    PowerSettings power;
    CStateSettings cstates;
    MainWindowSettings mainwin;
    VfSettings vf;
    bool valid = false;
};

std::string configDir();
std::string configPath();

Settings loadSettings();

bool saveSettings(const Settings& s);

bool saveFivr(const FivrSettings& f);
bool saveTurbo(const TurboSettings& t);
bool savePower(const PowerSettings& p);
bool saveCStates(const CStateSettings& c);
bool saveMainWindow(const MainWindowSettings& m);
bool saveVf(const VfSettings& v);

}
