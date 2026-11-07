

#include "gui.h"
#include "i18n.h"
#include "dialogs.h"
#include "tray.h"

#include "../include/undervolt.hpp"
#include "../include/powerprofile.hpp"
#include "../include/settings.hpp"
#include "../include/msr_daemon.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TS_WIDTH  767
#define TS_HEIGHT 609

static double g_scale = 1.0;

static int S(int v) { return (int)(v * g_scale); }
double ts_scale(void) { return g_scale; }

typedef struct {
    GtkWidget* window;

    GtkWidget* profile_radio[4];
    GtkWidget* profile_label[4];

    GtkWidget* chk_high_perf;
    GtkWidget* chk_clock_mod;
    GtkWidget* edit_clock_mod;
    GtkWidget* spin_clock_mod;
    GtkWidget* chk_set_mult;
    GtkWidget* edit_set_mult;
    GtkWidget* spin_set_mult;
    GtkWidget* chk_speed_shift;
    GtkWidget* edit_epp;
    GtkWidget* chk_power_saver;
    GtkWidget* chk_slfm;
    GtkWidget* btn_power_profile;
    GtkWidget* btn_stop_data;
    GtkWidget* chk_disable_turbo;
    GtkWidget* chk_speedstep;
    GtkWidget* chk_bd_prochot;
    GtkWidget* chk_c1e;
    GtkWidget* chk_task_bar;
    GtkWidget* chk_on_top;
    GtkWidget* chk_log_file;
    GtkWidget* chk_more_data;
    GtkWidget* btn_new_profile;

    GtkWidget* lbl_cpu_name;
    GtkWidget* lbl_vid;
    GtkWidget* lbl_mult;
    GtkWidget* lbl_freq;

    GtkWidget* monitor_grid;
    GtkWidget* monitor_cells[5][16];
    int monitor_rows;

    GtkWidget* edit_c0;
    GtkWidget* edit_temp;
    GtkWidget* edit_temp_max;
    GtkWidget* edit_pkg_power;
    GtkWidget* edit_pkg_max;
    GtkWidget* lbl_prochot;

    GtkWidget* btn_limits;
    GtkWidget* btn_fivr;
    GtkWidget* btn_tpl;
    GtkWidget* left_panel;
    GtkWidget* right_panel;
    GtkWidget* btn_c10;
    GtkWidget* btn_clr;
    GtkWidget* btn_minus1;
    GtkWidget* btn_minus2;
    GtkWidget* btn_save;
    GtkWidget* btn_options;
    GtkWidget* btn_turn_on;
    GtkWidget* btn_ts_bench;
    GtkWidget* btn_battery;
    GtkWidget* btn_gpu;

    uv::Controller* ctl;
    guint timer_id;
    int profile;
    double temp_max_seen;
    double pkg_max_seen;
    double mult_max_seen;
    int core_temp_max[16];
} AppState;

static AppState g_app;

static GtkWidget* mk_check(const char* text) {
    GtkWidget* c;
    if (text) c = gtk_check_button_new_with_label(tr(text));
    else c = gtk_check_button_new();
    gtk_widget_add_css_class(c, "ts-check");
    return c;
}

static GtkWidget* mk_btn(const char* text) {
    GtkWidget* b = gtk_button_new_with_label(tr(text));
    gtk_widget_add_css_class(b, "ts-btn");
    return b;
}

static GtkWidget* mk_lbl(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
    return l;
}

static GtkWidget* mk_edit_ro(void) {
    GtkWidget* e = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(e), FALSE);
    gtk_widget_add_css_class(e, "ts-data");
    return e;
}

static GtkWidget* mk_label_ro(void) {
    GtkWidget* lbl = gtk_label_new("");
    gtk_widget_add_css_class(lbl, "ts-readout");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.5f);
    return lbl;
}

static void put(GtkWidget* fixed, GtkWidget* w, int x, int y, int w_, int h_) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    gtk_widget_set_size_request(w, S(w_), S(h_));
}

static GtkWidget* red_group(const char* title) {
    GtkWidget* f = gtk_frame_new(NULL);
    gtk_widget_add_css_class(f, "ts-red-group");
    GtkWidget* lbl = gtk_label_new(tr(title));
    gtk_widget_add_css_class(lbl, "ts-red-title");
    gtk_frame_set_label_widget(GTK_FRAME(f), lbl);
    return f;
}

static void on_profile_changed(GtkCheckButton* b, gpointer d) {
    if (!gtk_check_button_get_active(b)) return;
    g_app.profile = GPOINTER_TO_INT(d);

    auto st = uv::loadSettings();
    st.mainwin.activeProfile = g_app.profile;
    uv::saveSettings(st);
}

static void on_disable_turbo_toggled(GtkCheckButton* c, gpointer d) {
    if (!g_app.ctl) return;
    const gboolean off = gtk_check_button_get_active(c);
    try {
        g_app.ctl->setTurbo(!off);
    } catch (...) {}
}

static void on_clock_mod_toggled(GtkCheckButton* c, gpointer d) {
    if (!g_app.ctl) return;
    const gboolean on = gtk_check_button_get_active(c);

    try {
        g_app.ctl->writeClockMod(on ? 100 : 100);
    } catch (...) {}
}

static void on_set_mult_toggled(GtkCheckButton* c, gpointer d) {

    if (!g_app.ctl) return;
    const gboolean on = gtk_check_button_get_active(c);

    if (on) {
        try {
            if (auto v = uv::MsrClient::readMsr(0x770)) {
                const int ratio = static_cast<int>((*v >> 8) & 0xFF);
                if (ratio > 0 && g_app.spin_set_mult) {
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_app.spin_set_mult),
                                              ratio);
                }
            }
        } catch (...) {}
    }
}

static void on_power_saver_toggled(GtkCheckButton* c, gpointer d) {

    if (!g_app.ctl) return;
    (void)gtk_check_button_get_active(c);
}

static void on_slfm_toggled(GtkCheckButton* c, gpointer d) {
    if (!g_app.ctl) return;
    const gboolean on = gtk_check_button_get_active(c);
    try {
        g_app.ctl->setSlfm(on);
    } catch (...) {}
}

static void on_speedstep_toggled(GtkCheckButton* c, gpointer d) {
    if (!g_app.ctl) return;
    const gboolean on = gtk_check_button_get_active(c);
    try {
        g_app.ctl->setSpeedStep(on);
    } catch (...) {}
}

static void on_bdprochot_toggled(GtkCheckButton* c, gpointer d) {
    if (!g_app.ctl) return;
    const gboolean on = gtk_check_button_get_active(c);
    try {
        g_app.ctl->setBdProchot(on);
    } catch (...) {}
}

static void on_c1e_toggled(GtkCheckButton* c, gpointer d) {
    if (!g_app.ctl) return;
    const gboolean on = gtk_check_button_get_active(c);
    try {
        auto cfg = g_app.ctl->readCStateConfig();
        if (cfg.ok && !cfg.locked) {

            cfg.c1Demotion = on;
            g_app.ctl->writeCStateConfig(cfg);
        }
    } catch (...) {}
}

static void on_epp_changed(GtkSpinButton* s, gpointer d) {
    if (!g_app.ctl) return;
    try {
        g_app.ctl->setEpp(gtk_spin_button_get_value_as_int(s));
    } catch (...) {}
}

static void on_set_mult_changed(GtkSpinButton* s, gpointer d) {
    if (!g_app.ctl) return;
    try {
        std::array<int, 8> r;
        r.fill(-1);
        const int v = gtk_spin_button_get_value_as_int(s);
        for (int i = 0; i < 8; ++i) r[i] = v;
        g_app.ctl->setTurboRatios(r);
    } catch (...) {}
}

static void on_turn_on_off(GtkButton* b, gpointer d) {

}

static void on_open_fivr(GtkButton* b, gpointer d) {
    GtkWidget* dlg = dlg_fivr_control_new(GTK_WINDOW(g_app.window));
    gtk_window_present(GTK_WINDOW(dlg));
}

static void on_open_options(GtkButton* b, gpointer d) {
    GtkWidget* dlg = dlg_options_new(GTK_WINDOW(g_app.window));
    gtk_window_present(GTK_WINDOW(dlg));
}

static void on_open_cstates(GtkButton* b, gpointer d) {
    GtkWidget* dlg = dlg_cstates_new(GTK_WINDOW(g_app.window));
    gtk_window_present(GTK_WINDOW(dlg));
}

static void on_open_percore(GtkButton* b, gpointer d) {
    GtkWidget* dlg = dlg_per_core_new(GTK_WINDOW(g_app.window));
    gtk_window_present(GTK_WINDOW(dlg));
}

static void on_open_tpl(GtkButton* b, gpointer d) {
    GtkWidget* dlg = dlg_turbo_power_new(GTK_WINDOW(g_app.window));
    gtk_window_present(GTK_WINDOW(dlg));
}

static void on_open_limits(GtkButton* b, gpointer d) {
    GtkWidget* dlg = dlg_limit_reasons_new(GTK_WINDOW(g_app.window));
    gtk_window_present(GTK_WINDOW(dlg));
}

static void do_action_fivr(GSimpleAction*, GVariant*, gpointer)   { on_open_fivr(nullptr, nullptr); }
static void do_action_tpl(GSimpleAction*, GVariant*, gpointer)    { on_open_tpl(nullptr, nullptr); }
static void do_action_limits(GSimpleAction*, GVariant*, gpointer) { on_open_limits(nullptr, nullptr); }
static void do_action_cstates(GSimpleAction*, GVariant*, gpointer){ on_open_cstates(nullptr, nullptr); }
static void do_action_percore(GSimpleAction*, GVariant*, gpointer){ on_open_percore(nullptr, nullptr); }
static void do_action_options(GSimpleAction*, GVariant*, gpointer){ on_open_options(nullptr, nullptr); }
static void do_action_hide(GSimpleAction*, GVariant*, gpointer)   {
    if (g_app.window) gtk_widget_set_visible(g_app.window, FALSE);
}
static void do_action_noop(GSimpleAction*, GVariant*, gpointer)   { }

static void install_accels(GtkApplication* app) {
    g_printerr("[accels] enter, app=%p\n", (void*)app);
    if (!app) return;

    struct { const char* name; void (*fn)(GSimpleAction*, GVariant*, gpointer); } kActs[] = {
        {"fivr",        do_action_fivr},
        {"tpl",         do_action_tpl},
        {"limits",      do_action_limits},
        {"cstates",     do_action_cstates},
        {"percore",     do_action_percore},
        {"options",     do_action_options},
        {"toggle-data", do_action_noop},
        {"hide",        do_action_hide},
        {"refresh",     do_action_noop},
    };
    for (const auto& a : kActs) {
        GSimpleAction* act = g_simple_action_new(a.name, nullptr);
        GCallback cb = reinterpret_cast<GCallback>(a.fn);
        g_signal_connect(act, "activate", cb, nullptr);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act));
        g_object_unref(act);
    }

    struct { const char* accel; const char* action; } kAccels[] = {
        {"<Control>f", "app.fivr"},
        {"<Control>t", "app.tpl"},
        {"<Control>l", "app.limits"},
        {"<Control>c", "app.cstates"},
        {"<Control>p", "app.percore"},
        {"<Control>o", "app.options"},
        {"<Control>r", "app.toggle-data"},
        {"<Control>q", "app.hide"},
        {"F5",         "app.refresh"},
    };
    for (const auto& a : kAccels) {
        const char* acc[2] = { a.accel, nullptr };
        gtk_application_set_accels_for_action(app, a.action, acc);
    }
    g_printerr("[accels] done, %zu actions, %zu accels\n",
               sizeof(kActs)/sizeof(kActs[0]), sizeof(kAccels)/sizeof(kAccels[0]));
}

static void on_clr(GtkButton* b, gpointer d) {
    if (!g_app.ctl) return;

    try {
        g_app.ctl->clearLimitReasons();
    } catch (...) {}
    g_app.temp_max_seen = 0;
    g_app.pkg_max_seen = 0;
    g_app.mult_max_seen = 0;
    memset(g_app.core_temp_max, 0, sizeof(g_app.core_temp_max));

    if (g_app.lbl_prochot) {
        gtk_label_set_text(GTK_LABEL(g_app.lbl_prochot), "已清除统计与限流位");
    }
}

static gboolean g_appEnabled = TRUE;

static void on_turn_off(GtkButton* b, gpointer d) {
    g_appEnabled = !g_appEnabled;
    gtk_button_set_label(b, g_appEnabled ? "Turn On" : "Turn Off");
    if (g_app.lbl_prochot) {
        gtk_label_set_text(GTK_LABEL(g_app.lbl_prochot),
            g_appEnabled ? "已恢复 MSR 写入"
                         : "已停止 MSR 写入（硬件保持当前状态）");
    }
}

static void append_monitor_row(void) {

    const int r = g_app.monitor_rows;
    if (r >= 16) return;
    ++g_app.monitor_rows;
    static const int kColW[5] = {70, 70, 70, 70, 70};
    for (int c = 0; c < 5; ++c) {
        GtkWidget* l = gtk_label_new("-");
        gtk_widget_set_halign(l, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(l, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(l, "ts-data-cell");
        gtk_widget_set_size_request(l, kColW[c], 16);
        g_app.monitor_cells[c][r] = l;
        gtk_grid_attach(GTK_GRID(g_app.monitor_grid), l, c, r, 1, 1);
    }
}

static gboolean on_timer(gpointer data) {
    if (!g_app.ctl) return G_SOURCE_CONTINUE;

    const auto rp = g_app.ctl->sampleRaplPower();

    uv::Sample s;
    try {
        s = g_app.ctl->sample();
    } catch (...) {
        return G_SOURCE_CONTINUE;
    }

    char buf[96];

    double vid = 0, mult = 0;
    if (auto fv = g_app.ctl->readFreqVid()) {
        vid = fv->vid;
        mult = fv->multiplier;

    }
    double bclk = 100.0;
    if (auto b = g_app.ctl->readBclk()) bclk = *b;

    snprintf(buf, sizeof(buf), "VID %.4f V", vid);
    gtk_label_set_text(GTK_LABEL(g_app.lbl_vid), buf);
    snprintf(buf, sizeof(buf), "%.2f x %.3f MHz", mult, bclk);
    gtk_label_set_text(GTK_LABEL(g_app.lbl_mult), buf);
    snprintf(buf, sizeof(buf), "%.2f MHz", mult * bclk);
    gtk_label_set_text(GTK_LABEL(g_app.lbl_freq), buf);

    const auto cores = g_app.ctl->samplePerCore();
    for (size_t i = 0; i < cores.size() && i < 16; ++i) {
        const int row = static_cast<int>(i) + 1;
        if (row >= 16) break;
        const auto& pc = cores[i];

        if (g_app.monitor_cells[0][row]) {
            if (pc.valid && pc.fid > 0) {
                snprintf(buf, sizeof(buf), "%d", pc.fid);
            } else {
                snprintf(buf, sizeof(buf), "-");
            }
            gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[0][row]), buf);
        }

        if (g_app.monitor_cells[1][row]) {
            if (pc.valid && pc.c0pct > 0) {
                snprintf(buf, sizeof(buf), "%.1f", pc.c0pct);
            } else {
                snprintf(buf, sizeof(buf), "-");
            }
            gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[1][row]), buf);
        }

        if (g_app.monitor_cells[2][row]) {
            snprintf(buf, sizeof(buf), "%.1f", pc.modPct);
            gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[2][row]), buf);
        }

        if (g_app.monitor_cells[3][row]) {
            if (pc.valid && pc.tempC > 0) {
                snprintf(buf, sizeof(buf), "%d", pc.tempC);
                gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[3][row]), buf);
            } else {
                gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[3][row]), "-");
            }
        }

        if (g_app.monitor_cells[4][row]) {
            if (pc.valid && pc.tempC > 0) {
                if (pc.tempC > g_app.core_temp_max[i]) {
                    g_app.core_temp_max[i] = pc.tempC;
                }
                snprintf(buf, sizeof(buf), "%d", g_app.core_temp_max[i]);
                gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[4][row]), buf);
            } else {
                gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[4][row]), "-");
            }
        }
    }

    const int r0 = 1;

    if (const auto cm = g_app.ctl->readClockMod(); cm.ok) {
        if (cm.enabled) {

            const double pct = 100.0 * (16 - cm.dutyCycle) / 16.0;
            snprintf(buf, sizeof(buf), "%.1f", pct);
        } else {
            snprintf(buf, sizeof(buf), "100.0");
        }
        gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[2][r0]), buf);
    }

    double curTemp = 0;
    bool hasTemp = false;
    if (auto t = g_app.ctl->readTemperatureC()) {
        curTemp = *t;
        hasTemp = true;
    } else if (s.tempSensorOk) {
        curTemp = s.packageTemp;
        hasTemp = true;
    }
    if (hasTemp) {
        snprintf(buf, sizeof(buf), "%d°C", (int)curTemp);
        gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[3][r0]), buf);
        if (curTemp > g_app.temp_max_seen) g_app.temp_max_seen = curTemp;
        snprintf(buf, sizeof(buf), "%d°C", (int)g_app.temp_max_seen);
        gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[4][r0]), buf);
    }

    if (hasTemp) {
        snprintf(buf, sizeof(buf), "%d °C", (int)curTemp);
        gtk_editable_set_text(GTK_EDITABLE(g_app.edit_temp), buf);
        if (curTemp > g_app.temp_max_seen) g_app.temp_max_seen = curTemp;
        snprintf(buf, sizeof(buf), "%d °C", (int)g_app.temp_max_seen);
        gtk_editable_set_text(GTK_EDITABLE(g_app.edit_temp_max), buf);
    }

    double c0 = 0;
    bool hasC0 = false;
    if (auto c = g_app.ctl->sampleC0Percent()) { c0 = *c; hasC0 = true; }
    if (hasC0) {
        snprintf(buf, sizeof(buf), "%.1f", c0);
        gtk_editable_set_text(GTK_EDITABLE(g_app.edit_c0), buf);
        gtk_label_set_text(GTK_LABEL(g_app.monitor_cells[1][1]), buf);
    }

    static bool capsChecked = false;
    if (!capsChecked && g_app.ctl) {
        capsChecked = true;

        if (g_app.chk_slfm && !g_app.ctl->readSlfm()) {
            gtk_widget_set_visible(g_app.chk_slfm, FALSE);
        }
    }

    if (rp.ok) {
        snprintf(buf, sizeof(buf), "%.1f W", rp.pkgW);
        gtk_label_set_text(GTK_LABEL(g_app.edit_pkg_power), buf);

        if (rp.pkgW > g_app.pkg_max_seen) g_app.pkg_max_seen = rp.pkgW;
        snprintf(buf, sizeof(buf), "%.1f W", g_app.pkg_max_seen);
        gtk_label_set_text(GTK_LABEL(g_app.edit_pkg_max), buf);
    } else {

        gtk_label_set_text(GTK_LABEL(g_app.edit_pkg_power), "--");
    }

    return G_SOURCE_CONTINUE;
}

static GtkWidget* build_left_panel(void) {
    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(348), S(TS_HEIGHT));

    GtkWidget* titlebar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(titlebar, "ts-titlebar");
    GtkWidget* logo = gtk_label_new("ReThrottleStop");
    gtk_widget_add_css_class(logo, "ts-logo");
    gtk_widget_set_hexpand(logo, TRUE);
    gtk_widget_set_halign(logo, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(titlebar), logo);

    GtkWidget* btn_close = gtk_button_new_with_label("X");
    gtk_widget_add_css_class(btn_close, "ts-close");
    g_signal_connect(btn_close, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer) {
            gtk_widget_set_visible(g_app.window, FALSE);
        }), NULL);
    gtk_box_append(GTK_BOX(titlebar), btn_close);

    GtkWidget* handle = gtk_window_handle_new();
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), titlebar);
    put(fixed, handle, 8, 30, 328, 26);

    GtkWidget* g_perf = red_group("Performance");
    put(fixed, g_perf, 14, 148, 320, 60);
    for (int i = 0; i < 4; ++i) {
        GtkWidget* rb = gtk_check_button_new();
        if (i > 0) {
            gtk_check_button_set_group(GTK_CHECK_BUTTON(rb),
                                       GTK_CHECK_BUTTON(g_app.profile_radio[0]));
        }
        g_signal_connect(rb, "toggled", G_CALLBACK(on_profile_changed),
                         GINT_TO_POINTER(i));
        g_app.profile_radio[i] = rb;
        put(fixed, rb, 55 + i * 62, 166, 16, 16);

        GtkWidget* nm = gtk_label_new("");
        gtk_widget_add_css_class(nm, "ts-profilename");
        gtk_label_set_xalign(GTK_LABEL(nm), 0.5f);
        put(fixed, nm, 30 + i * 62, 186, 66, 14);
        g_app.profile_label[i] = nm;
    }

    {
        const auto st0 = uv::loadSettings();
        for (int i = 0; i < 4; ++i) {
            char def[16];
            snprintf(def, sizeof(def), "Profile %d", i + 1);
            const char* nm = (st0.valid && !st0.mainwin.profileName[i].empty())
                             ? st0.mainwin.profileName[i].c_str() : def;
            if (g_app.profile_label[i])
                gtk_label_set_text(GTK_LABEL(g_app.profile_label[i]), nm);
        }
    }

    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_app.profile_radio[0]), TRUE);

    GtkWidget* g_set = red_group("Settings");
    put(fixed, g_set, 14, 216, 320, 330);

    int y = 236;

    put(fixed, mk_lbl("Power Profile"), 26, y, 110, 20);

    {
        GtkWidget* menubtn = gtk_menu_button_new();
        gtk_widget_add_css_class(menubtn, "ts-menubtn");

        const auto cur = uv::PowerProfile::current();
        const char* curName = cur.ok ? uv::PowerProfile::levelName(cur.level)
                                     : "Balanced";
        gtk_menu_button_set_label(GTK_MENU_BUTTON(menubtn), curName);

        GtkWidget* pop = gtk_popover_new();
        gtk_widget_add_css_class(pop, "ts-popover");
        GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(vbox, "ts-popover-box");

        struct { const char* name; uv::PowerProfile::Level lv; } items[] = {
            {"Performance", uv::PowerProfile::Level::Performance},
            {"Balanced",    uv::PowerProfile::Level::Balanced},
            {"Power Saver", uv::PowerProfile::Level::PowerSaver},
        };
        for (int i = 0; i < 3; ++i) {
            GtkWidget* row = gtk_button_new_with_label(items[i].name);
            gtk_widget_add_css_class(row, "ts-popover-row");
            g_object_set_data(G_OBJECT(row), "lv",
                              GINT_TO_POINTER(static_cast<int>(items[i].lv)));
            g_object_set_data(G_OBJECT(row), "mb", menubtn);
            g_signal_connect(row, "clicked",
                G_CALLBACK(+[](GtkButton* b, gpointer) {
                    const auto lv = static_cast<uv::PowerProfile::Level>(
                        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "lv")));
                    uv::PowerProfile::set(lv);
                    auto* mb = GTK_MENU_BUTTON(
                        g_object_get_data(G_OBJECT(b), "mb"));
                    if (mb) {
                        gtk_menu_button_set_label(mb,
                            uv::PowerProfile::levelName(lv));
                        GtkPopover* p2 = gtk_menu_button_get_popover(mb);
                        if (p2) gtk_popover_popdown(p2);
                    }
                }), NULL);
            gtk_box_append(GTK_BOX(vbox), row);
        }

        gtk_popover_set_child(GTK_POPOVER(pop), vbox);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menubtn), pop);
        put(fixed, menubtn, 140, y, 168, 24);
    }
    y += 26;

    g_app.chk_clock_mod = mk_check("Clock Mod");
    g_signal_connect(g_app.chk_clock_mod, "toggled", G_CALLBACK(on_clock_mod_toggled), NULL);
    put(fixed, g_app.chk_clock_mod, 26, y, 180, 20);
    g_app.edit_clock_mod = mk_edit_ro();
    gtk_editable_set_text(GTK_EDITABLE(g_app.edit_clock_mod), "100.0%");
    put(fixed, g_app.edit_clock_mod, 200, y, 52, 20);

    GtkWidget* cm_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* cm_up = gtk_button_new_with_label("\u25b2");
    GtkWidget* cm_dn = gtk_button_new_with_label("\u25bc");
    gtk_widget_add_css_class(cm_up, "ts-arrow");
    gtk_widget_add_css_class(cm_dn, "ts-arrow");
    gtk_box_append(GTK_BOX(cm_box), cm_up);
    gtk_box_append(GTK_BOX(cm_box), cm_dn);
    put(fixed, cm_box, 256, y, 26, 20);
    y += 24;

    g_app.chk_set_mult = mk_check("Set Multiplier");
    g_signal_connect(g_app.chk_set_mult, "toggled", G_CALLBACK(on_set_mult_toggled), NULL);
    put(fixed, g_app.chk_set_mult, 26, y, 180, 20);
    g_app.edit_set_mult = mk_edit_ro();
    gtk_editable_set_text(GTK_EDITABLE(g_app.edit_set_mult), "22");
    put(fixed, g_app.edit_set_mult, 200, y, 52, 20);
    GtkWidget* sm_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* sm_up = gtk_button_new_with_label("\u25b2");
    GtkWidget* sm_dn = gtk_button_new_with_label("\u25bc");
    gtk_widget_add_css_class(sm_up, "ts-arrow");
    gtk_widget_add_css_class(sm_dn, "ts-arrow");
    gtk_box_append(GTK_BOX(sm_box), sm_up);
    gtk_box_append(GTK_BOX(sm_box), sm_dn);
    put(fixed, sm_box, 256, y, 26, 20);
    y += 24;

    g_app.chk_speed_shift = mk_check("Speed Shift - EPP");
    put(fixed, g_app.chk_speed_shift, 26, y, 180, 20);
    g_app.edit_epp = mk_edit_ro();
    gtk_editable_set_text(GTK_EDITABLE(g_app.edit_epp), "128");
    put(fixed, g_app.edit_epp, 200, y, 52, 20);
    y += 24;

    g_app.chk_power_saver = mk_check("Power Saver");
    g_signal_connect(g_app.chk_power_saver, "toggled", G_CALLBACK(on_power_saver_toggled), NULL);
    put(fixed, g_app.chk_power_saver, 26, y, 180, 20);

    g_app.btn_stop_data = mk_btn("Stop Data");
    put(fixed, g_app.btn_stop_data, 254, y - 2, 66, 24);
    y += 26;

    g_app.chk_slfm = mk_check("SLFM");
    g_signal_connect(g_app.chk_slfm, "toggled", G_CALLBACK(on_slfm_toggled), NULL);
    put(fixed, g_app.chk_slfm, 26, y, 180, 20);
    y += 22;

    g_app.chk_disable_turbo = mk_check("Disable Turbo");
    g_signal_connect(g_app.chk_disable_turbo, "toggled",
                     G_CALLBACK(on_disable_turbo_toggled), NULL);
    put(fixed, g_app.chk_disable_turbo, 26, y, 175, 20);

    g_app.chk_speedstep = mk_check("SpeedStep");
    g_signal_connect(g_app.chk_speedstep, "toggled", G_CALLBACK(on_speedstep_toggled), NULL);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_app.chk_speedstep), TRUE);
    put(fixed, g_app.chk_speedstep, 205, y, 110, 20);
    y += 22;

    g_app.chk_bd_prochot = mk_check("BD PROCHOT");
    g_signal_connect(g_app.chk_bd_prochot, "toggled", G_CALLBACK(on_bdprochot_toggled), NULL);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_app.chk_bd_prochot), TRUE);
    put(fixed, g_app.chk_bd_prochot, 26, y, 175, 20);
    g_app.chk_c1e = mk_check("C1E");
    g_signal_connect(g_app.chk_c1e, "toggled", G_CALLBACK(on_c1e_toggled), NULL);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_app.chk_c1e), TRUE);
    put(fixed, g_app.chk_c1e, 205, y, 110, 20);
    y += 22;

    g_app.chk_task_bar = mk_check("Task Bar");
    put(fixed, g_app.chk_task_bar, 26, y, 175, 20);
    g_app.chk_on_top = mk_check("On Top");
    put(fixed, g_app.chk_on_top, 205, y, 110, 20);
    y += 22;

    g_app.chk_log_file = mk_check("Log File");
    put(fixed, g_app.chk_log_file, 26, y, 175, 20);
    g_app.chk_more_data = mk_check("More Data");
    put(fixed, g_app.chk_more_data, 205, y, 110, 20);
    y += 22;

    return fixed;
}

static GtkWidget* build_right_panel(void) {
    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(TS_WIDTH) - S(348), S(TS_HEIGHT));

    std::string cpuTitle = uv::cpuModelName();
    const auto pos = cpuTitle.find("Core");
    if (pos != std::string::npos) cpuTitle = cpuTitle.substr(pos);
    GtkWidget* g_cpu = red_group(cpuTitle.c_str());
    put(fixed, g_cpu, 10, 42, 392, 118);

    {
        const auto topo = uv::Controller().readCoreTopology();
        if (topo.hybrid) {
            char tb[96];
            snprintf(tb, sizeof(tb), "P: %d核 @%.1fGHz  E: %d核 @%.1fGHz",
                     topo.pCores, topo.pMaxKHz / 1e6,
                     topo.eCores, topo.eMaxKHz / 1e6);
            GtkWidget* tl = gtk_label_new(tb);
            gtk_widget_add_css_class(tl, "ts-topo");
            put(fixed, tl, 10, 60, 392, 16);
        }
    }
    g_app.lbl_vid = gtk_label_new("VID 0.7262 V");
    gtk_widget_set_halign(g_app.lbl_vid, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(g_app.lbl_vid, "ts-info");
    put(fixed, g_app.lbl_vid, 10, 78, 392, 18);
    g_app.lbl_mult = gtk_label_new("38.05 x 99.768 MHz");
    gtk_widget_set_halign(g_app.lbl_mult, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(g_app.lbl_mult, "ts-info");
    put(fixed, g_app.lbl_mult, 10, 96, 392, 18);
    g_app.lbl_freq = gtk_label_new("3796.18 MHz");
    gtk_widget_set_halign(g_app.lbl_freq, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(g_app.lbl_freq, "ts-info");
    put(fixed, g_app.lbl_freq, 10, 114, 392, 18);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_app.monitor_grid = gtk_grid_new();
    g_app.monitor_rows = 0;

    const char* cols[] = {"FID", "C0%", "Mod", "°C", "Max"};
    static const int kHeadW[5] = {70, 70, 70, 70, 70};
    for (int c = 0; c < 5; ++c) {
        GtkWidget* h = gtk_label_new(cols[c]);
        gtk_widget_add_css_class(h, "ts-table-head");
        gtk_widget_set_halign(h, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request(h, kHeadW[c], 18);
        gtk_grid_attach(GTK_GRID(g_app.monitor_grid), h, c, 0, 1, 1);
    }

    for (int r = 0; r < 16; ++r) append_monitor_row();

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), g_app.monitor_grid);
    gtk_widget_set_hexpand(scroll, FALSE);
    gtk_widget_set_vexpand(scroll, FALSE);
    gtk_widget_add_css_class(scroll, "ts-table-frame");
    put(fixed, scroll, 10, 164, 392, 234);

    put(fixed, mk_lbl("C0%"), 10, 410, 50, 18);
    g_app.edit_c0 = mk_edit_ro();
    put(fixed, g_app.edit_c0, 72, 408, 90, 22);
    g_app.edit_temp = mk_edit_ro();
    put(fixed, g_app.edit_temp, 172, 408, 90, 22);
    put(fixed, mk_lbl("Max"), 274, 410, 40, 18);
    g_app.edit_temp_max = mk_edit_ro();
    put(fixed, g_app.edit_temp_max, 312, 408, 90, 22);

    put(fixed, mk_lbl("PKG Power"), 10, 440, 90, 18);
    g_app.edit_pkg_power = mk_label_ro();
    put(fixed, g_app.edit_pkg_power, 122, 438, 90, 22);
    put(fixed, mk_lbl("Max"), 274, 440, 40, 18);
    g_app.edit_pkg_max = mk_label_ro();
    put(fixed, g_app.edit_pkg_max, 312, 438, 90, 22);

    g_app.btn_limits = mk_btn("Limits");
    g_signal_connect(g_app.btn_limits, "clicked", G_CALLBACK(on_open_limits), NULL);
    put(fixed, g_app.btn_limits, 10, 470, 100, 26);
    g_app.lbl_prochot = gtk_label_new("");
    gtk_widget_add_css_class(g_app.lbl_prochot, "ts-prochot");
    put(fixed, g_app.lbl_prochot, 152, 472, 240, 22);

    g_app.btn_fivr = mk_btn("FIVR");
    g_signal_connect(g_app.btn_fivr, "clicked", G_CALLBACK(on_open_fivr), NULL);
    put(fixed, g_app.btn_fivr, 10, 506, 108, 28);
    g_app.btn_tpl = mk_btn("TPL");
    g_signal_connect(g_app.btn_tpl, "clicked", G_CALLBACK(on_open_tpl), NULL);
    put(fixed, g_app.btn_tpl, 124, 506, 108, 28);
    g_app.btn_c10 = mk_btn("C10");
    g_signal_connect(g_app.btn_c10, "clicked", G_CALLBACK(on_open_cstates), NULL);
    put(fixed, g_app.btn_c10, 238, 506, 82, 28);
    g_app.btn_clr = mk_btn("CLR");
    g_signal_connect(g_app.btn_clr, "clicked", G_CALLBACK(on_clr), NULL);
    put(fixed, g_app.btn_clr, 326, 506, 56, 28);
    g_app.btn_minus1 = mk_btn("-");
    g_signal_connect(g_app.btn_minus1, "clicked",
        G_CALLBACK(+[](GtkButton* b, gpointer) {

            GtkRoot* r = gtk_widget_get_root(GTK_WIDGET(b));
            if (!GTK_IS_WINDOW(r)) return;
            static gboolean collapsed = FALSE;
            collapsed = !collapsed;

            gtk_window_set_default_size(GTK_WINDOW(r),
                collapsed ? 767 : 767,
                collapsed ? 60 : 609);
        }), NULL);
    put(fixed, g_app.btn_minus1, 390, 506, 24, 28);

    g_app.btn_save = mk_btn("Save");
    put(fixed, g_app.btn_save, 0, 548, 118, 28);
    g_app.btn_options = mk_btn("Options");
    put(fixed, g_app.btn_options, 0, 548, 118, 28);
    g_app.btn_turn_on = mk_btn("Turn On");
    g_signal_connect(g_app.btn_turn_on, "clicked", G_CALLBACK(on_turn_off), NULL);
    put(fixed, g_app.btn_turn_on, 0, 548, 118, 28);
    g_app.btn_minus2 = mk_btn("-");
    g_signal_connect(g_app.btn_minus2, "clicked",
        G_CALLBACK(+[](GtkButton* b, gpointer) {
            GtkRoot* r = gtk_widget_get_root(GTK_WIDGET(b));
            if (!GTK_IS_WINDOW(r)) return;
            static gboolean collapsed2 = FALSE;
            collapsed2 = !collapsed2;

            if (g_app.right_panel)
                gtk_widget_set_visible(g_app.right_panel, !collapsed2);
            if (g_app.left_panel)
                gtk_widget_set_size_request(g_app.left_panel,
                    S(348), collapsed2 ? S(120) : S(TS_HEIGHT));
        }), NULL);
    put(fixed, g_app.btn_minus2, 390, 548, 24, 28);

    return fixed;
}

GtkWidget* test_build(void);
static GtkWidget* build_main_window(void) {

    GtkWidget* root = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(root), 0);
    gtk_grid_set_row_spacing(GTK_GRID(root), 0);

    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);

    gtk_widget_set_size_request(root, S(TS_WIDTH), S(TS_HEIGHT));

    GtkWidget* left = build_left_panel();
    gtk_widget_set_hexpand(left, FALSE);
    gtk_widget_set_vexpand(left, TRUE);
    gtk_grid_attach(GTK_GRID(root), left, 0, 0, 1, 1);

    GtkWidget* right = build_right_panel();
    g_app.right_panel = right;
    gtk_widget_set_size_request(right, S(TS_WIDTH) - S(348), S(TS_HEIGHT));
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_vexpand(right, TRUE);
    gtk_grid_attach(GTK_GRID(root), right, 1, 0, 1, 1);

    gtk_widget_set_size_request(left, S(348), -1);

    return root;
}

int uv_gui_run(int argc, char** argv) {
    try {
        uv::MsrClient::ensureDaemon();
        g_app.ctl = new uv::Controller();
    } catch (...) {
        g_app.ctl = nullptr;
    }
    g_app.profile = 0;
    g_app.temp_max_seen = 0;
    g_app.pkg_max_seen = 0;
    g_app.mult_max_seen = 0;
    memset(g_app.core_temp_max, 0, sizeof(g_app.core_temp_max));
    g_app.monitor_rows = 0;

    GtkApplication* app = gtk_application_new("org.rethrottlestop.app",
                                              G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(+[](GtkApplication* a, gpointer) {
        GtkWidget* win = gtk_application_window_new(a);
        gtk_window_set_title(GTK_WINDOW(win), "ReThrottleStop");
        gtk_window_set_default_size(GTK_WINDOW(win), S(TS_WIDTH), S(TS_HEIGHT));

        gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
        g_app.window = win;

        GtkCssProvider* css = gtk_css_provider_new();
        gtk_css_provider_load_from_string(css,
            "window { background: #101010; }"
            "label { color: #d0d0d0; font-size: 9px; }"
            "button { min-height: 0; padding: 1px 4px; font-size: 9px;"
            "         background: #2a2a2a; color: #e0e0e0; border: 1px solid #666;"
            "         border-radius: 2px; }"
            "button:hover { background: #3a3a3a; }"
            "button:active { background: #555; }"
            "checkbutton { color: #d0d0d0; font-size: 9px; }"
            "checkbutton label { padding-left: 4px; }"
            "entry { min-height: 0; padding: 0 2px; font-size: 9px;"
            "        background: #181818; color: #00cc00; border: 1px solid #333; }"

            "button.ts-udbtn { min-width: 0; min-height: 0;"
            "  padding: 0; margin: 0; font-size: 7px;"
            "  border-radius: 1px; }"
            "spinbutton { min-height: 0; font-size: 9px; background: #181818; }"
            "spinbutton entry { color: #00cc00; min-width: 0; }"

            "spinbutton.ts-spin entry { min-width: 24px; padding: 0 2px; }"
            "spinbutton.ts-spin button { min-width: 18px; min-height: 0;"
            "  padding: 0; margin: 0; }"
            "spinbutton.ts-spin button.image-button { min-width: 18px; }"
            "grid label { padding: 0 6px; }"
            "label.ts-readout {"
            "  background: #1a1a1a; color: #00cc00;"
            "  border: 1px solid #555; border-radius: 2px;"
            "  padding: 2px 6px; font-size: 10px; }"
            "label.ts-data-cell { color: #00cc00; font-size: 9px; }"
            "label.ts-table-head { color: #fff; background: #333;"
            "                      font-weight: bold; }"
            "label.ts-info { color: #e0e0e0; font-size: 11px; }"
            "label.ts-topo { color: #80b0ff; font-size: 9px; }"
            "label.ts-untrusted { color: #ffaa00; font-style: italic; }"
            "label.ts-logo { color: #888; font-size: 18px; font-weight: bold;"
            "                font-family: monospace; }"
            "label.ts-prochot { color: #ff3030; font-size: 11px; font-weight: bold; }"
            "frame.ts-red-group { border: 1px solid #c04040; border-radius: 3px;"
            "                     background: rgba(60,10,10,0.25); }"
            "frame.ts-red-group > label { color: #ff5050; font-weight: bold;"
            "                             font-size: 9px; }"
            "scrolledwindow { border: 1px solid #444; background: #101010; }"

            "scrolledwindow.ts-table-frame {"
            "  border: 1px solid #808080; border-radius: 2px;"
            "  background: #0a0a0a; }"
            "scrolledwindow.ts-table-frame grid {"
            "  border: 1px solid #555; }"
            "label.ts-data-cell {"
            "  border-right: 1px solid #2a2a2a;"
            "  border-bottom: 1px solid #1a1a1a; }"
            "label.ts-table-head {"
            "  border-right: 1px solid #555;"
            "  border-bottom: 1px solid #808080; }"

            "frame.ts-limit-col {"
            "  border: 1px solid #606060; border-radius: 2px;"
            "  background: rgba(20,20,20,0.6); }"
            "label.ts-limit-colhead {"
            "  color: #ff6060; font-size: 9px; font-weight: bold; }"
            "separator.ts-limit-vline {"
            "  background: #a04040; min-width: 1px; }"
            "separator.ts-limit-hline {"
            "  background: #502020; min-height: 1px; }"
            "window.ts-traymenu { background: #181818;"
            "                     border: 1px solid #555; border-radius: 4px; }"
            "button.ts-menuitem { background: transparent;"
            "  border: none; border-radius: 2px;"
            "  min-height: 0; padding: 6px 14px; font-size: 11px;"
            "  color: #d0d0d0; }"
            "button.ts-menuitem:hover { background: #264f78; color: #fff; }"
            "label.ts-menutitle { color: #888; font-size: 9px;"
            "  padding: 4px 14px 2px; }"
            "label.ts-limit-on { color: #ff3030; font-weight: bold; }"
            "label.ts-limit-past { color: #cc8800; }"

            "box.ts-dragbar { background: #1a1a1a;"
            "                 border-bottom: 1px solid #333; }"

            "menubutton.ts-menubtn > button {"
            "  background: #181818; color: #00cc00;"
            "  border: 1px solid #555; border-radius: 2px;"
            "  min-height: 0; padding: 2px 8px; font-size: 10px; }"
            "menubutton.ts-menubtn > button:hover { background: #2a2a2a; }"
            "popover.ts-popover { background: #101010; border: 1px solid #555; }"
            "popover.ts-popover > contents { background: #101010; }"
            "box.ts-popover-box { background: #101010; padding: 2px; }"
            "button.ts-popover-row {"
            "  background: transparent; color: #d0d0d0;"
            "  border: none; border-radius: 2px;"
            "  min-height: 0; padding: 4px 10px; font-size: 10px; }"
            "button.ts-popover-row:hover { background: #264f78; color: #ffffff; }"

            "entry.ts-locked { background: #3a3a3a; color: #e8e8e8;"
            "                  border: 1px solid #2a2a2a; }"
            "entry.ts-edit { background: #181818; color: #e0e0e0;"
            "                border: 1px solid #555; }"

            "scale > trough { background: #333; border-radius: 2px; }"
            "scale > trough > slider { background: #4a90d9;"
            "                          border-radius: 2px;"
            "                          border: none; box-shadow: none; }"
            "button.ts-arrow { min-width: 12px; min-height: 9px; padding: 0;"
            "                 font-size: 6px; }"
            "box.ts-titlebar { background: #1a1a1a; border-bottom: 1px solid #333; }"
            "button.ts-close { min-width: 20px; min-height: 20px; padding: 0;"
            "                 background: #5a2020; color: #fff; font-weight: bold;"
            "                 border: 1px solid #802020; border-radius: 2px; }"
            "button.ts-close:hover { background: #a02020; }"
        );
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        GtkWidget* content = build_main_window();
        gtk_window_set_child(GTK_WINDOW(win), content);
        gtk_window_present(GTK_WINDOW(win));

        g_signal_connect(win, "close-request",
            G_CALLBACK(+[](GtkWindow* w, gpointer) -> gboolean {

                auto st = uv::loadSettings();
                st.mainwin.clockMod = g_app.chk_clock_mod
                    ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_clock_mod)) : false;
                st.mainwin.setMultiplier = g_app.chk_set_mult
                    ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_set_mult)) : false;
                st.mainwin.speedShift = g_app.chk_speed_shift
                    ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_speed_shift)) : false;
                st.mainwin.powerSaver = g_app.chk_power_saver
                    ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_power_saver)) : false;
                st.mainwin.disableTurbo = g_app.chk_disable_turbo
                    ? !gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_disable_turbo)) : false;
                st.mainwin.speedStep = g_app.chk_speedstep
                    ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_speedstep)) : true;
                st.mainwin.bdProchot = g_app.chk_bd_prochot
                    ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_bd_prochot)) : true;
                st.mainwin.c1e = g_app.chk_c1e
                    ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_app.chk_c1e)) : true;
                if (g_app.edit_epp) {
                    const char* v = gtk_editable_get_text(GTK_EDITABLE(g_app.edit_epp));
                    if (v && *v) st.mainwin.epp = atoi(v);
                }

                if (g_app.chk_task_bar)
                    st.mainwin.taskBar = gtk_check_button_get_active(
                        GTK_CHECK_BUTTON(g_app.chk_task_bar));
                if (g_app.chk_on_top)
                    st.mainwin.onTop = gtk_check_button_get_active(
                        GTK_CHECK_BUTTON(g_app.chk_on_top));
                if (g_app.chk_log_file)
                    st.mainwin.logFile = gtk_check_button_get_active(
                        GTK_CHECK_BUTTON(g_app.chk_log_file));
                if (g_app.chk_more_data)
                    st.mainwin.moreData = gtk_check_button_get_active(
                        GTK_CHECK_BUTTON(g_app.chk_more_data));

                if (g_app.spin_clock_mod)
                    st.mainwin.clockModPercent = gtk_spin_button_get_value_as_int(
                        GTK_SPIN_BUTTON(g_app.spin_clock_mod));

                if (g_app.spin_set_mult)
                    st.mainwin.multiplier = gtk_spin_button_get_value_as_int(
                        GTK_SPIN_BUTTON(g_app.spin_set_mult));
                uv::saveSettings(st);
                gtk_widget_set_visible(GTK_WIDGET(w), FALSE);
                return TRUE;
            }), NULL);

        if (!tray_init(win)) {
            g_printerr("tray: not available, window stays visible\n");
        }

        g_app.timer_id = g_timeout_add(1000, on_timer, NULL);
        install_accels(a);

    }), NULL);

    const int status = g_application_run(G_APPLICATION(app), 0, nullptr);
    if (g_app.timer_id) g_source_remove(g_app.timer_id);
    g_object_unref(app);
    delete g_app.ctl;
    return status;
}

GtkWidget* test_build(void) { return build_main_window(); }
