

#include "../include/powerprofile.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <gio/gio.h>

namespace uv {

namespace {

constexpr const char* kPpdBus = "net.hadess.PowerProfiles";
constexpr const char* kPpdPath = "/net/hadess/PowerProfiles";
constexpr const char* kPpdIface = "net.hadess.PowerProfiles";

bool setViaPpd(const char* profile) {
    GError* err = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    if (!conn) {
        if (err) g_error_free(err);
        return false;
    }

    GVariant* reply = g_dbus_connection_call_sync(
        conn, kPpdBus, kPpdPath, "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", kPpdIface, "ActiveProfile",
                      g_variant_new_string(profile)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);

    const bool ok = (reply != nullptr);
    if (reply) g_variant_unref(reply);
    if (err) g_error_free(err);
    g_object_unref(conn);
    return ok;
}

std::optional<std::string> getViaPpd() {
    GError* err = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    if (!conn) {
        if (err) g_error_free(err);
        return std::nullopt;
    }

    GVariant* reply = g_dbus_connection_call_sync(
        conn, kPpdBus, kPpdPath, "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", kPpdIface, "ActiveProfile"),
        nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);

    std::optional<std::string> result;
    if (reply) {
        GVariant* inner = nullptr;
        g_variant_get(reply, "(v)", &inner);
        if (inner) {
            const char* s = g_variant_get_string(inner, nullptr);
            if (s) result = std::string(s);
            g_variant_unref(inner);
        }
        g_variant_unref(reply);
    }
    if (err) g_error_free(err);
    g_object_unref(conn);
    return result;
}

bool setViaSysfs(const char* profile) {
    FILE* f = std::fopen("/sys/firmware/acpi/platform_profile", "w");
    if (!f) return false;
    const bool ok = (std::fprintf(f, "%s\n", profile) > 0);
    std::fclose(f);
    return ok;
}

bool setGovernor(const char* gov) {
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "echo %s | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor "
                  "> /dev/null 2>&1", gov);
    return std::system(cmd) == 0;
}

}

PowerProfile::State PowerProfile::current() {
    State p;

    if (auto s = getViaPpd()) {
        p.source = "power-profiles-daemon";
        if (*s == "performance")      p.level = Level::Performance;
        else if (*s == "power-saver") p.level = Level::PowerSaver;
        else                          p.level = Level::Balanced;
        p.ok = true;
        return p;
    }

    FILE* f = std::fopen(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (f) {
        char buf[32] = {0};
        if (std::fgets(buf, sizeof(buf), f)) {
            p.source = "cpufreq governor";
            p.ok = true;
            if (std::strncmp(buf, "performance", 11) == 0)
                p.level = Level::Performance;
            else
                p.level = Level::Balanced;
        }
        std::fclose(f);
    }
    return p;
}

bool PowerProfile::set(Level level) {
    const char* ppd = nullptr;
    const char* gov = nullptr;

    switch (level) {
        case Level::Performance:
            ppd = "performance"; gov = "performance"; break;
        case Level::Balanced:
            ppd = "balanced";    gov = "powersave";   break;
        case Level::PowerSaver:
            ppd = "power-saver"; gov = "powersave";   break;
    }

    if (setViaPpd(ppd)) return true;

    if (setViaSysfs(ppd)) return true;

    return setGovernor(gov);
}

const char* PowerProfile::levelName(Level level) {
    switch (level) {
        case Level::Performance: return "Performance";
        case Level::Balanced:    return "Balanced";
        case Level::PowerSaver:  return "Power Saver";
    }
    return "?";
}

}
