

#include "i18n.h"

#include <string.h>
#include <stdlib.h>

static int g_lang_zh = 1;

typedef struct {
    const char* key;
    const char* zh;
} Entry;

static const Entry kEntries[] = {

    {"Undervolt Go", "Undervolt Go"},
    {"FIVR Control", "FIVR Control"},
    {"Turbo Power Limits", "Turbo Power Limits"},
    {"Turbo Ratio Limits", "Turbo Ratio Limits"},
    {"Status", "状态"},
    {"Settings", "设置"},
    {"About", "关于"},

    {"CPU Core", "CPU Core"},
    {"Intel GPU", "Intel GPU"},
    {"CPU Cache", "CPU Cache"},
    {"System Agent", "System Agent"},
    {"Analog IO", "Analog IO"},
    {"Digital IO", "Digital IO"},
    {"Offset Voltage (mV)", "Offset Voltage (mV)"},
    {"Voltage", "Voltage"},
    {"Range", "Range"},
    {"125 mV", "125 mV"},
    {"250 mV", "250 mV"},
    {"1000 mV", "1000 mV"},
    {"Apply", "应用"},
    {"Reset", "重置"},
    {"Zero All", "全部归零"},
    {"Summary", "Summary"},
    {"Name", "Name"},
    {"Mode", "Mode"},
    {"Offset", "Offset"},
    {"Adaptive", "Adaptive"},
    {"Default", "Default"},
    {"Locked", "已锁定"},
    {"Unlocked", "未锁定"},

    {"P1 Power Limit", "P1 Power Limit"},
    {"P2 Power Limit", "P2 Power Limit"},
    {"Long Term", "Long Term"},
    {"Short Term", "Short Term"},
    {"Power (W)", "Power (W)"},
    {"Time (s)", "Time (s)"},
    {"Lock power limit", "锁定功率限制"},
    {"Enable", "启用"},

    {"Package Temp", "Package Temp"},
    {"Core Max Temp", "Core Max Temp"},
    {"Fan Speed", "Fan Speed"},
    {"Frequency", "Frequency"},
    {"Package Power", "Package Power"},
    {"Turbo", "Turbo"},
    {"No sensor", "无传感器"},
    {"N/A", "N/A"},
    {"CPU Info", "CPU Info"},
    {"Threads", "Threads"},
    {"Temperature", "Temperature"},

    {"Temperature Target (°C)", "Temperature Target (°C)"},
    {"Force positive offsets", "允许正偏移"},
    {"Verbose output", "详细输出"},
    {"Backend", "后端"},
    {"Profile", "配置"},
    {"Save", "保存"},
    {"Load", "载入"},
    {"Language", "语言"},

    {"Ready", "就绪"},
    {"Settings applied successfully.", "设置已成功应用。"},
    {"Failed to apply settings.", "应用设置失败。"},
    {"Positive offset requires force.", "正偏移需要先启用「允许正偏移」。"},
    {"Undervolting is locked by BIOS.", "降压已被 BIOS 锁定。"},
    {"No MSR access. Run as root or setcap.", "无 MSR 访问权限，请以 root 运行或设置 capability。"},
    {"Daemon running", "守护进程运行中"},
    {"Daemon not running", "守护进程未运行"},

    {NULL, NULL},
};

void i18n_set_lang(const char* lang) {
    if (!lang) return;
    g_lang_zh = (strncmp(lang, "en", 2) != 0);
}

const char* tr(const char* key) {
    if (!key) return "";
    if (!g_lang_zh) return key;

    for (const Entry* e = kEntries; e->key; ++e) {
        if (strcmp(e->key, key) == 0) {
            return e->zh;
        }
    }
    return key;
}
