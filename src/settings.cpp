

#include "../include/settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace uv {

namespace {

const char* xdgConfigHome() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    if (x && *x) return x;
    return nullptr;
}

const char* homeDir() {
    return std::getenv("HOME");
}

struct IniLine {
    std::string section;
    std::string key;
    std::string value;
};

bool parseIni(const std::string& path, std::vector<IniLine>* out) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;

    char line[512];
    std::string section;
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);

        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();

        size_t b = s.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        s = s.substr(b);
        if (s[0] == '#' || s[0] == ';') continue;

        if (s[0] == '[') {
            const size_t e = s.find(']');
            if (e != std::string::npos) section = s.substr(1, e - 1);
            continue;
        }
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;

        IniLine l;
        l.section = section;
        l.key = s.substr(0, eq);
        l.value = s.substr(eq + 1);

        while (!l.key.empty() && (l.key.back() == ' ' || l.key.back() == '\t'))
            l.key.pop_back();
        while (!l.value.empty() && l.value.front() == ' ')
            l.value.erase(0, 1);
        out->push_back(l);
    }
    std::fclose(f);
    return true;
}

const char* findValue(const std::vector<IniLine>& lines,
                      const char* section, const char* key) {
    for (const auto& l : lines) {
        if (l.section == section && l.key == key) return l.value.c_str();
    }
    return nullptr;
}

int getInt(const std::vector<IniLine>& ls, const char* sec, const char* key, int def) {
    const char* v = findValue(ls, sec, key);
    if (!v) return def;
    char* end = nullptr;
    const long n = std::strtol(v, &end, 10);
    if (end == v) return def;
    return static_cast<int>(n);
}

double getDouble(const std::vector<IniLine>& ls, const char* sec, const char* key, double def) {
    const char* v = findValue(ls, sec, key);
    if (!v) return def;
    char* end = nullptr;
    const double d = std::strtod(v, &end);
    if (end == v) return def;
    return d;
}

bool getBool(const std::vector<IniLine>& ls, const char* sec, const char* key, bool def) {
    const char* v = findValue(ls, sec, key);
    if (!v) return def;
    return (std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0 ||
            std::strcmp(v, "yes") == 0 || std::strcmp(v, "on") == 0);
}

}

std::string configDir() {
    if (const char* x = xdgConfigHome()) {
        return std::string(x) + "/rethrottlestop";
    }
    if (const char* h = homeDir()) {
        return std::string(h) + "/.config/rethrottlestop";
    }
    return "/tmp/rethrottlestop";
}

std::string configPath() { return configDir() + "/settings.conf"; }

Settings loadSettings() {
    Settings s;
    const std::string path = configPath();

    std::vector<IniLine> ls;
    if (!parseIni(path, &ls)) {
        return s;
    }
    s.valid = true;

    s.fivr.domain     = getInt(ls, "fivr", "domain", 0);
    s.fivr.adaptive   = getBool(ls, "fivr", "adaptive", true);
    s.fivr.rangeMv    = getInt(ls, "fivr", "range_mv", 125);
    s.fivr.iccMax     = getInt(ls, "fivr", "iccmax", 0);
    s.fivr.cacheRatio = getInt(ls, "fivr", "cache_ratio", 0);
    s.fivr.vccinRange = getInt(ls, "fivr", "vccin_range", 0);
    s.fivr.saveMode   = getInt(ls, "fivr", "save_mode", 0);
    s.fivr.unlocked   = getBool(ls, "fivr", "unlocked", false);
    for (int i = 0; i < 6; ++i) {
        char k[32];
        std::snprintf(k, sizeof(k), "offset%d", i);
        s.fivr.offsets[i] = getDouble(ls, "fivr", k, 0.0);
    }

    s.fivr.avxOffset   = getInt(ls, "misc", "avx_offset", 0);
    s.fivr.tvb         = getBool(ls, "misc", "tvb", false);
    s.fivr.ringDownBin = getBool(ls, "misc", "ring_down_bin", false);
    s.fivr.vMaxStress  = getBool(ls, "misc", "vmax_stress", false);
    s.fivr.mvBoost     = getBool(ls, "misc", "mv_boost", false);
    s.fivr.mvBoostMv   = getInt(ls, "misc", "mv_boost_mv", 0);
    s.fivr.overclock   = getBool(ls, "misc", "overclock", false);

    for (int i = 0; i < 8; ++i) {
        char k[32];
        std::snprintf(k, sizeof(k), "ratio%d", i);
        s.turbo.ratios[i] = getInt(ls, "turbo", k, 0);
    }
    s.turbo.extraVoltage = getInt(ls, "turbo", "extra_voltage", 0);
    s.turbo.ntrLock      = getBool(ls, "turbo", "ntr_lock", false);
    s.turbo.ntrRatio1    = getInt(ls, "turbo", "ntr_ratio1", 0);
    s.turbo.ntrRatio2    = getInt(ls, "turbo", "ntr_ratio2", 0);
    s.turbo.unlimited    = getBool(ls, "turbo", "unlimited", false);
    s.turbo.perfCoreType = getInt(ls, "turbo", "perf_core_type", 0);

    s.power.pl1W        = getDouble(ls, "power", "pl1_w", 0);
    s.power.pl2W        = getDouble(ls, "power", "pl2_w", 0);
    s.power.pl1WindowUs = getDouble(ls, "power", "pl1_window_us", 0);
    s.power.pl2WindowUs = getDouble(ls, "power", "pl2_window_us", 0);

    s.cstates.cStateEnable   = getBool(ls, "cstates", "cstate_enable", false);
    s.cstates.c1Demotion     = getBool(ls, "cstates", "c1_demotion", false);
    s.cstates.c3Demotion     = getBool(ls, "cstates", "c3_demotion", false);
    s.cstates.c1Undemotion   = getBool(ls, "cstates", "c1_undemotion", false);
    s.cstates.c3Undemotion   = getBool(ls, "cstates", "c3_undemotion", false);
    s.cstates.pkgCStateLimit = getInt(ls, "cstates", "pkg_cstate_limit", 0);

    s.mainwin.clockMod        = getBool(ls, "mainwindow", "clock_mod", false);
    s.mainwin.clockModPercent = getInt(ls, "mainwindow", "clock_mod_percent", 100);
    s.mainwin.setMultiplier   = getBool(ls, "mainwindow", "set_multiplier", false);
    s.mainwin.multiplier      = getInt(ls, "mainwindow", "multiplier", 0);
    s.mainwin.speedShift      = getBool(ls, "mainwindow", "speed_shift", false);
    s.mainwin.epp             = getInt(ls, "mainwindow", "epp", 128);
    s.mainwin.powerSaver      = getBool(ls, "mainwindow", "power_saver", false);
    s.mainwin.disableTurbo    = getBool(ls, "mainwindow", "disable_turbo", false);
    s.mainwin.speedStep       = getBool(ls, "mainwindow", "speedstep", true);
    s.mainwin.bdProchot       = getBool(ls, "mainwindow", "bd_prochot", true);
    s.mainwin.c1e             = getBool(ls, "mainwindow", "c1e", true);
    s.mainwin.taskBar         = getBool(ls, "mainwindow", "task_bar", false);
    s.mainwin.onTop           = getBool(ls, "mainwindow", "on_top", false);
    s.mainwin.logFile         = getBool(ls, "mainwindow", "log_file", false);
    s.mainwin.moreData        = getBool(ls, "mainwindow", "more_data", false);
    s.mainwin.powerProfile    = getInt(ls, "mainwindow", "power_profile", 1);
    s.mainwin.activeProfile   = getInt(ls, "mainwindow", "active_profile", 0);
    for (int i = 0; i < 4; ++i) {
        char k[32];
        std::snprintf(k, sizeof(k), "profile_name%d", i);
        const char* v = findValue(ls, "mainwindow", k);
        if (v && *v) s.mainwin.profileName[i] = v;
    }

    s.vf.domain = getInt(ls, "vf", "domain", 0);
    for (int r = 0; r < 11; ++r) {
        for (int p = 0; p < 4; ++p) {
            char k[32];
            std::snprintf(k, sizeof(k), "p%d_%d", r, p);
            s.vf.points[r][p] = getInt(ls, "vf", k, 0);
        }
    }

    return s;
}

bool saveSettings(const Settings& s) {
    const std::string dir = configDir();
    const std::string path = configPath();
    const std::string tmp = path + ".tmp";

    ::mkdir(dir.c_str(), 0755);

    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return false;

    std::fprintf(f, "# ReThrottleStop settings\n");
    std::fprintf(f, "# 自动生成，可手动编辑\n\n");

    std::fprintf(f, "[fivr]\n");
    std::fprintf(f, "domain=%d\n", s.fivr.domain);
    std::fprintf(f, "adaptive=%d\n", s.fivr.adaptive ? 1 : 0);
    std::fprintf(f, "range_mv=%d\n", s.fivr.rangeMv);
    std::fprintf(f, "iccmax=%d\n", s.fivr.iccMax);
    std::fprintf(f, "cache_ratio=%d\n", s.fivr.cacheRatio);
    std::fprintf(f, "vccin_range=%d\n", s.fivr.vccinRange);
    std::fprintf(f, "save_mode=%d\n", s.fivr.saveMode);
    std::fprintf(f, "unlocked=%d\n", s.fivr.unlocked ? 1 : 0);
    for (int i = 0; i < 6; ++i) {
        std::fprintf(f, "offset%d=%.4f\n", i, s.fivr.offsets[i]);
    }

    std::fprintf(f, "\n[misc]\n");
    std::fprintf(f, "avx_offset=%d\n", s.fivr.avxOffset);
    std::fprintf(f, "tvb=%d\n", s.fivr.tvb ? 1 : 0);
    std::fprintf(f, "ring_down_bin=%d\n", s.fivr.ringDownBin ? 1 : 0);
    std::fprintf(f, "vmax_stress=%d\n", s.fivr.vMaxStress ? 1 : 0);
    std::fprintf(f, "mv_boost=%d\n", s.fivr.mvBoost ? 1 : 0);
    std::fprintf(f, "mv_boost_mv=%d\n", s.fivr.mvBoostMv);
    std::fprintf(f, "overclock=%d\n", s.fivr.overclock ? 1 : 0);

    std::fprintf(f, "\n[turbo]\n");
    for (int i = 0; i < 8; ++i) {
        std::fprintf(f, "ratio%d=%d\n", i, s.turbo.ratios[i]);
    }
    std::fprintf(f, "extra_voltage=%d\n", s.turbo.extraVoltage);
    std::fprintf(f, "ntr_lock=%d\n", s.turbo.ntrLock ? 1 : 0);
    std::fprintf(f, "ntr_ratio1=%d\n", s.turbo.ntrRatio1);
    std::fprintf(f, "ntr_ratio2=%d\n", s.turbo.ntrRatio2);
    std::fprintf(f, "unlimited=%d\n", s.turbo.unlimited ? 1 : 0);
    std::fprintf(f, "perf_core_type=%d\n", s.turbo.perfCoreType);

    std::fprintf(f, "\n[power]\n");
    std::fprintf(f, "pl1_w=%.2f\n", s.power.pl1W);
    std::fprintf(f, "pl2_w=%.2f\n", s.power.pl2W);
    std::fprintf(f, "pl1_window_us=%.0f\n", s.power.pl1WindowUs);
    std::fprintf(f, "pl2_window_us=%.0f\n", s.power.pl2WindowUs);

    std::fprintf(f, "\n[cstates]\n");
    std::fprintf(f, "cstate_enable=%d\n", s.cstates.cStateEnable ? 1 : 0);
    std::fprintf(f, "c1_demotion=%d\n", s.cstates.c1Demotion ? 1 : 0);
    std::fprintf(f, "c3_demotion=%d\n", s.cstates.c3Demotion ? 1 : 0);
    std::fprintf(f, "c1_undemotion=%d\n", s.cstates.c1Undemotion ? 1 : 0);
    std::fprintf(f, "c3_undemotion=%d\n", s.cstates.c3Undemotion ? 1 : 0);
    std::fprintf(f, "pkg_cstate_limit=%d\n", s.cstates.pkgCStateLimit);

    std::fprintf(f, "\n[mainwindow]\n");
    std::fprintf(f, "clock_mod=%d\n", s.mainwin.clockMod ? 1 : 0);
    std::fprintf(f, "clock_mod_percent=%d\n", s.mainwin.clockModPercent);
    std::fprintf(f, "set_multiplier=%d\n", s.mainwin.setMultiplier ? 1 : 0);
    std::fprintf(f, "multiplier=%d\n", s.mainwin.multiplier);
    std::fprintf(f, "speed_shift=%d\n", s.mainwin.speedShift ? 1 : 0);
    std::fprintf(f, "epp=%d\n", s.mainwin.epp);
    std::fprintf(f, "power_saver=%d\n", s.mainwin.powerSaver ? 1 : 0);
    std::fprintf(f, "disable_turbo=%d\n", s.mainwin.disableTurbo ? 1 : 0);
    std::fprintf(f, "speedstep=%d\n", s.mainwin.speedStep ? 1 : 0);
    std::fprintf(f, "bd_prochot=%d\n", s.mainwin.bdProchot ? 1 : 0);
    std::fprintf(f, "c1e=%d\n", s.mainwin.c1e ? 1 : 0);
    std::fprintf(f, "task_bar=%d\n", s.mainwin.taskBar ? 1 : 0);
    std::fprintf(f, "on_top=%d\n", s.mainwin.onTop ? 1 : 0);
    std::fprintf(f, "log_file=%d\n", s.mainwin.logFile ? 1 : 0);
    std::fprintf(f, "more_data=%d\n", s.mainwin.moreData ? 1 : 0);
    std::fprintf(f, "power_profile=%d\n", s.mainwin.powerProfile);
    std::fprintf(f, "active_profile=%d\n", s.mainwin.activeProfile);
    for (int i = 0; i < 4; ++i) {
        if (!s.mainwin.profileName[i].empty()) {
            std::fprintf(f, "profile_name%d=%s\n", i,
                         s.mainwin.profileName[i].c_str());
        }
    }

    std::fprintf(f, "\n[vf]\n");
    std::fprintf(f, "domain=%d\n", s.vf.domain);
    for (int r = 0; r < 11; ++r) {
        for (int p = 0; p < 4; ++p) {
            std::fprintf(f, "p%d_%d=%d\n", r, p, s.vf.points[r][p]);
        }
    }

    const bool ok = (std::fclose(f) == 0);
    if (!ok) {
        ::unlink(tmp.c_str());
        return false;
    }

    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool saveFivr(const FivrSettings& f) {
    Settings s = loadSettings();
    s.fivr = f;
    return saveSettings(s);
}

bool saveTurbo(const TurboSettings& t) {
    Settings s = loadSettings();
    s.turbo = t;
    return saveSettings(s);
}

bool savePower(const PowerSettings& p) {
    Settings s = loadSettings();
    s.power = p;
    return saveSettings(s);
}

bool saveCStates(const CStateSettings& c) {
    Settings s = loadSettings();
    s.cstates = c;
    return saveSettings(s);
}

bool saveMainWindow(const MainWindowSettings& m) {
    Settings s = loadSettings();
    s.mainwin = m;
    return saveSettings(s);
}

bool saveVf(const VfSettings& v) {
    Settings s = loadSettings();
    s.vf = v;
    return saveSettings(s);
}

}
