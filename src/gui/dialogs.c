

#include "dialogs.h"
#include "i18n.h"

#include "../include/undervolt.hpp"
#include "../include/ts_spec.hpp"
#include "../include/msr_daemon.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern double ts_scale(void);
#define S(v) ((int)((v) * ts_scale()))

static uv::Controller* g_ctl = nullptr;

static uv::Controller* get_ctl(void) {
    if (!g_ctl) {
        try { uv::MsrClient::ensureDaemon(); g_ctl = new uv::Controller(); } catch (...) { g_ctl = nullptr; }
    }
    return g_ctl;
}

static GtkWidget* dlg_button(const char* text) {
    GtkWidget* b = gtk_button_new_with_label(tr(text));
    gtk_widget_add_css_class(b, "ts-btn");
    return b;
}

static GtkWidget* dlg_label(const char* text) {
    GtkWidget* l = gtk_label_new(tr(text));
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

static GtkWidget* dlg_edit(void) {
    return gtk_entry_new();
}

static GtkWidget* dlg_spin(double min, double max) {
    return gtk_spin_button_new_with_range(min, max, 1);
}

static void put(GtkWidget* fixed, GtkWidget* w, int x, int y, int wd, int ht) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    gtk_widget_set_size_request(w, S(wd), S(ht));
}

static GtkWidget* group_box(const char* title) {
    GtkWidget* frame = gtk_frame_new(tr(title));
    gtk_widget_add_css_class(frame, "ts-group");
    return frame;
}

typedef struct {
    GtkWidget* btn_unlock;
    GtkWidget* radio_domain[6];
    GtkWidget* edit_voltage;
    GtkWidget* scale_voltage;
    GtkWidget* spin_voltage;
    GtkWidget* edit_offset;
    GtkWidget* scale_offset;
    GtkWidget* spin_offset;
    GtkWidget* edit_cache;
    GtkWidget* scale_cache;
    GtkWidget* spin_cache;
    int domain;
    gboolean updating;
} FivrPanel;

static void fivr_on_domain(GtkToggleButton* b, gpointer d) {
    FivrPanel* p = (FivrPanel*)d;
    if (!gtk_toggle_button_get_active(b)) return;
    p->domain = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "idx"));

    uv::Controller* c = get_ctl();
    if (!c) return;
    try {
        const double v = c->readOffset(static_cast<uv::Plane>(p->domain));
        p->updating = TRUE;
        gtk_range_set_value(GTK_RANGE(p->scale_offset), v);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->spin_offset), v);
        p->updating = FALSE;
    } catch (...) {}
}

static void fivr_on_apply(GtkButton* b, gpointer d) {
    FivrPanel* p = (FivrPanel*)d;
    uv::Controller* c = get_ctl();
    if (!c) return;
    const double mv = gtk_spin_button_get_value(GTK_SPIN_BUTTON(p->spin_offset));
    try {
        c->setOffset(static_cast<uv::Plane>(p->domain), mv, false);
    } catch (...) {}
}

static void fivr_on_zero(GtkButton* b, gpointer d) {
    FivrPanel* p = (FivrPanel*)d;
    uv::Controller* c = get_ctl();
    if (!c) return;
    try {
        for (int i = 0; i < 5; ++i) c->setOffset(static_cast<uv::Plane>(i), 0.0, true);
    } catch (...) {}
}

static void fivr_on_reset(GtkButton* b, gpointer d) {
    FivrPanel* p = (FivrPanel*)d;
    uv::Controller* c = get_ctl();
    if (!c) return;
    try {
        const double v = c->readOffset(static_cast<uv::Plane>(p->domain));
        p->updating = TRUE;
        gtk_range_set_value(GTK_RANGE(p->scale_offset), v);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->spin_offset), v);
        p->updating = FALSE;
    } catch (...) {}
}

static void build_fivr_panel(GtkWidget* fixed, FivrPanel* p) {

    GtkWidget* grp = group_box("FIVR Control");
    put(fixed, grp, 199, 3, 177, 73);

    const char* domains[] = {"CPU Core", "Intel GPU", "CPU Cache",
                             "System Agent", "Analog IO", "Digital IO"};
    for (int i = 0; i < 6; ++i) {
        GtkWidget* rb = gtk_toggle_button_new();
        if (i == 0) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), TRUE);
        } else {
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(rb),
                                        GTK_TOGGLE_BUTTON(p->radio_domain[0]));
        }
        g_object_set_data(G_OBJECT(rb), "idx", GINT_TO_POINTER(i));
        g_signal_connect(rb, "toggled", G_CALLBACK(fivr_on_domain), p);
        p->radio_domain[i] = rb;

        const int col = i % 2;
        const int row = i / 2;
        put(fixed, rb, 209 + col * 80, 19 + row * 17, 12, 13);
        GtkWidget* lbl = dlg_label(domains[i]);
        put(fixed, lbl, 224 + col * 80, 19 + row * 17, 60, 13);
    }

    GtkWidget* grp2 = group_box("CPU Core");
    put(fixed, grp2, 199, 80, 177, 328);

    p->btn_unlock = dlg_button("Unlock Adjustable Voltage");
    put(fixed, p->btn_unlock, 209, 93, 154, 13);

    put(fixed, dlg_label("Voltage"), 209, 136, 72, 13);
    p->edit_voltage = dlg_edit();
    put(fixed, p->edit_voltage, 289, 136, 42, 13);
    put(fixed, dlg_label("V"), 335, 136, 28, 13);
    p->scale_voltage = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 2, 0.001);
    put(fixed, p->scale_voltage, 209, 156, 122, 15);
    p->spin_voltage = dlg_spin(0, 2);
    put(fixed, p->spin_voltage, 337, 156, 24, 12);

    put(fixed, dlg_label("Offset Voltage"), 209, 183, 74, 13);
    p->edit_offset = dlg_edit();
    put(fixed, p->edit_offset, 289, 183, 42, 13);
    put(fixed, dlg_label("mV"), 335, 183, 28, 13);
    p->scale_offset = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -125, 125, 0.1);
    put(fixed, p->scale_offset, 209, 202, 122, 15);
    p->spin_offset = dlg_spin(-125, 125);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(p->spin_offset), 1);
    put(fixed, p->spin_offset, 337, 202, 24, 12);

    g_signal_connect(p->scale_offset, "value-changed",
                     G_CALLBACK(+[](GtkRange* r, gpointer d) {
                         FivrPanel* p = (FivrPanel*)d;
                         if (p->updating) return;
                         p->updating = TRUE;
                         gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->spin_offset),
                                                   gtk_range_get_value(r));
                         p->updating = FALSE;
                     }), p);
    g_signal_connect(p->spin_offset, "value-changed",
                     G_CALLBACK(+[](GtkSpinButton* s, gpointer d) {
                         FivrPanel* p = (FivrPanel*)d;
                         if (p->updating) return;
                         p->updating = TRUE;
                         gtk_range_set_value(GTK_RANGE(p->scale_offset),
                                             gtk_spin_button_get_value(s));
                         p->updating = FALSE;
                     }), p);

    put(fixed, dlg_label("Cache Ratio"), 209, 360, 74, 13);
    p->edit_cache = dlg_edit();
    put(fixed, p->edit_cache, 289, 360, 42, 13);
    p->scale_cache = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 64, 1);
    put(fixed, p->scale_cache, 209, 380, 122, 15);
    p->spin_cache = dlg_spin(0, 64);
    put(fixed, p->spin_cache, 337, 380, 24, 12);
}

static GtkWidget* build_voltage_table(void) {
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    const char* cols[] = {"Domain", "Voltage", "Offset", "IccMax"};
    for (int c = 0; c < 4; ++c) {
        GtkWidget* h = dlg_label(cols[c]);
        gtk_widget_set_halign(h, GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(grid), h, c, 0, 1, 1);
    }
    const char* rows[] = {"CPU Core", "Intel GPU", "CPU Cache",
                          "System Agent", "Analog IO", "Digital IO"};
    for (int r = 0; r < 6; ++r) {
        gtk_grid_attach(GTK_GRID(grid), dlg_label(rows[r]), 0, r + 1, 1, 1);
        for (int c = 1; c < 4; ++c) {
            gtk_grid_attach(GTK_GRID(grid), dlg_label("--"), c, r + 1, 1, 1);
        }
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    return scroll;
}

typedef struct {
    GtkWidget* win;
    GtkWidget* edit_ratio[8];
    FivrPanel fivr;
    GtkWidget* profile_check[4];
    GtkWidget* edit_extra_voltage;
    GtkWidget* edit_overclock;
} TrlDialog;

static void trl_load_ratios(TrlDialog* d) {
    uv::Controller* c = get_ctl();
    if (!c) return;
    try {
        const auto ratios = c->readTurboRatios();
        char buf[16];
        for (int i = 0; i < 8; ++i) {
            if (ratios[i] > 0) {
                snprintf(buf, sizeof(buf), "%d", ratios[i]);
                gtk_editable_set_text(GTK_EDITABLE(d->edit_ratio[i]), buf);
            }
        }
    } catch (...) {}
}

static void trl_on_apply(GtkButton* b, gpointer d) {
    TrlDialog* dlg = (TrlDialog*)d;
    uv::Controller* c = get_ctl();
    if (!c) return;
    std::array<int, 8> ratios;
    ratios.fill(-1);
    for (int i = 0; i < 8; ++i) {
        const char* txt = gtk_editable_get_text(GTK_EDITABLE(dlg->edit_ratio[i]));
        if (txt && *txt) ratios[i] = atoi(txt);
    }
    try {
        c->setTurboRatios(ratios);
    } catch (...) {}
}

static void trl_on_ok(GtkButton* b, gpointer d) {
    trl_on_apply(b, d);
    TrlDialog* dlg = (TrlDialog*)d;
    gtk_window_destroy(GTK_WINDOW(dlg->win));
}

GtkWidget* dlg_turbo_ratio_new(GtkWindow* parent) {
    TrlDialog* d = new TrlDialog();
    memset(d, 0, sizeof(*d));

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), tr("Turbo Ratio Limits"));
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_default_size(GTK_WINDOW(win), S(646), S(440));
    d->win = win;

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(646), S(436));

    GtkWidget* grp_profile = group_box("Profile");
    put(fixed, grp_profile, 9, 3, 177, 35);
    for (int i = 0; i < 4; ++i) {
        GtkWidget* cb = gtk_check_button_new();
        g_signal_connect(cb, "toggled", G_CALLBACK(+[](GtkToggleButton* b, gpointer) {

        }), NULL);
        d->profile_check[i] = cb;
        put(fixed, cb, 34 + i * 40, 17, 12, 12);
    }

    GtkWidget* grp_etv = group_box("Extra Turbo Voltage");
    put(fixed, grp_etv, 9, 41, 177, 35);
    put(fixed, dlg_label("Extra Voltage"), 22, 56, 78, 12);
    d->edit_extra_voltage = dlg_edit();
    put(fixed, d->edit_extra_voltage, 109, 56, 36, 12);
    put(fixed, dlg_label("<= 255"), 150, 56, 32, 12);

    GtkWidget* grp_oc = group_box("Turbo Overclocking");
    put(fixed, grp_oc, 9, 118, 177, 35);
    d->edit_overclock = dlg_edit();
    put(fixed, d->edit_overclock, 58, 133, 80, 13);

    GtkWidget* grp_trl = group_box("Turbo Ratio Limits");
    put(fixed, grp_trl, 9, 195, 177, 173);
    for (int i = 0; i < 8; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "%d %s", i + 1, i == 0 ? "Core" : "Cores");
        put(fixed, dlg_label(label), 18, 226 + i * 17, 36, 12);
        d->edit_ratio[i] = dlg_edit();
        put(fixed, d->edit_ratio[i], 64, 226 + i * 17, 36, 12);
    }

    build_fivr_panel(fixed, &d->fivr);

    GtkWidget* table = build_voltage_table();
    put(fixed, table, 390, 9, 245, 166);

    put(fixed, dlg_label("Min / Max"), 405, 195, 80, 13);
    put(fixed, dlg_edit(), 491, 195, 58, 13);
    put(fixed, dlg_edit(), 559, 195, 58, 13);

    GtkWidget* grp_save = group_box("Save Voltage Changes");
    put(fixed, grp_save, 388, 343, 247, 65);
    for (int i = 0; i < 3; ++i) {
        put(fixed, gtk_check_button_new(), 405, 357 + i * 16, 12, 13);
    }

    GtkWidget* btn_ok = dlg_button("OK");
    g_object_set_data(G_OBJECT(btn_ok), "dlgwin", win);
    g_object_set_data(G_OBJECT(btn_ok), "dlgwin", win);
    g_signal_connect(btn_ok, "clicked", G_CALLBACK(trl_on_ok), d);
    put(fixed, btn_ok, 9, 416, 58, 15);

    GtkWidget* btn_reset = dlg_button("Reset Voltage");
    g_object_set_data(G_OBJECT(btn_reset), "dlgwin", win);
    g_object_set_data(G_OBJECT(btn_reset), "dlgwin", win);
    g_signal_connect(btn_reset, "clicked", G_CALLBACK(fivr_on_reset), &d->fivr);
    put(fixed, btn_reset, 208, 416, 78, 15);

    GtkWidget* btn_zero = dlg_button("Zero Offset");
    g_object_set_data(G_OBJECT(btn_zero), "dlgwin", win);
    g_object_set_data(G_OBJECT(btn_zero), "dlgwin", win);
    g_signal_connect(btn_zero, "clicked", G_CALLBACK(fivr_on_zero), &d->fivr);
    put(fixed, btn_zero, 290, 416, 78, 15);

    GtkWidget* btn_cancel = dlg_button("Cancel");
    g_object_set_data(G_OBJECT(btn_cancel), "dlgwin", win);
    g_object_set_data(G_OBJECT(btn_cancel), "dlgwin", win);
    g_signal_connect(btn_cancel, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer d) {
                         gtk_window_destroy(GTK_WINDOW(((TrlDialog*)d)->win));
                     }), d);
    put(fixed, btn_cancel, 473, 416, 77, 15);

    GtkWidget* btn_apply = dlg_button("Apply");
    g_object_set_data(G_OBJECT(btn_apply), "dlgwin", win);
    g_object_set_data(G_OBJECT(btn_apply), "dlgwin", win);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(trl_on_apply), d);
    put(fixed, btn_apply, 554, 416, 77, 15);

    {
        GtkWidget* rootbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget* handle = gtk_window_handle_new();
        GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(bar, "ts-dragbar");
        gtk_widget_set_size_request(bar, -1, S(18));
        gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), bar);
        gtk_box_append(GTK_BOX(rootbox), handle);
        gtk_box_append(GTK_BOX(rootbox), fixed);
        gtk_window_set_child(GTK_WINDOW(win), rootbox);
    };

    trl_load_ratios(d);

    g_object_set_data_full(G_OBJECT(win), "trl-dialog", d,
                           +[](gpointer p) { delete (TrlDialog*)p; });
    return win;
}

typedef struct {
    GtkWidget* win;

    GtkWidget* cells[3][13];
    GtkWidget* lbl_raw[3];
    GtkWidget* lbl_status;

    uint64_t last[3];
    uint64_t accum[3];

    unsigned int counts[3][13];
    gboolean alive;
    guint timer_id;
} LimitsDialog;

static const struct {
    int bit;
    const char* coreName;
    const char* gpuName;
    bool coreOnly;
} kLimitRows[13] = {
    { 0, "BD PROCHOT",  "BD PROCHOT",  false},
    { 1, "THERMAL",     "THERMAL",     false},
    { 4, "RESIDENCY",   "RESERVED",    false},
    { 5, "AVG THERMAL", "AVG THERMAL", false},
    { 6, "VR THERMAL",  "VR THERMAL",  false},
    { 7, "VR CURRENT",  "VR CURRENT",  false},

    { 8, "EDP OTHER",   "EDP OTHER",   false},
    { 9, "RESERVED",    "RESERVED",    false},
    {10, "PL 1",        "PL 1",        false},
    {11, "PL 2",        "PL 2",        false},

    {12, "EDP CURRENT", "EDP CURRENT", false},
    {14, "TVB",         "-",           true},
    {15, "V MAX",       "-",           true},
};

static const char* kGroupNames[3] = {"CORE", "GPU", "RING"};

static void limits_refresh(LimitsDialog* d) {
    uv::Controller* c = get_ctl();
    if (!c) return;

    std::optional<uv::Controller::LimitReason> opt;
    try {
        opt = c->readLimitReasons();
    } catch (...) { return; }
    if (!opt) {
        if (d->lbl_status) {
            char eb[128];
            snprintf(eb, sizeof(eb),
                     "MSR 0x64F/0x6B0/0x6B1 读取失败"
                     "（需 root 或 CAP_SYS_RAWIO）");
            gtk_label_set_text(GTK_LABEL(d->lbl_status), eb);
        }
        return;
    }
    const auto& r = *opt;

    const uint64_t raws[3] = { r.core.raw, r.gpu.raw, r.ring.raw };

    constexpr uint64_t kAccumMask = 0xFFFF;
    for (int g = 0; g < 3; ++g) {
        const uint64_t prev = d->last[g];
        const uint64_t delta = raws[g] ^ prev;
        d->accum[g] = (d->accum[g] | delta) & kAccumMask;
        d->last[g] = raws[g];

        for (int i = 0; i < 13; ++i) {
            const int bit = kLimitRows[i].bit;
            const bool was = (prev >> bit) & 1;
            const bool now = (raws[g] >> bit) & 1;
            if (!was && now) d->counts[g][i]++;
        }
    }

    int totalActive = 0;
    for (int g = 0; g < 3; ++g) {
        for (int i = 0; i < 13; ++i) {
            if (!d->cells[g][i]) continue;

            const bool coreGroup = (g == 0);
            if (!coreGroup && kLimitRows[i].coreOnly) {

                gtk_label_set_text(GTK_LABEL(d->cells[g][i]), "n/a");
                gtk_widget_remove_css_class(d->cells[g][i], "ts-limit-on");
                continue;
            }
            const bool nowOn = (raws[g] >> kLimitRows[i].bit) & 1;
            const bool everOn = (d->accum[g] >> kLimitRows[i].bit) & 1;
            const unsigned int n = d->counts[g][i];

            if (nowOn) {

                char cb[24];
                if (n > 1) snprintf(cb, sizeof(cb), "NOW ×%u", n);
                else       snprintf(cb, sizeof(cb), "NOW");
                gtk_label_set_text(GTK_LABEL(d->cells[g][i]), cb);
                gtk_widget_add_css_class(d->cells[g][i], "ts-limit-on");
                gtk_widget_remove_css_class(d->cells[g][i], "ts-limit-past");
                totalActive++;
            } else if (everOn) {

                char cb[24];
                snprintf(cb, sizeof(cb), "past ×%u", n);
                gtk_label_set_text(GTK_LABEL(d->cells[g][i]), cb);
                gtk_widget_remove_css_class(d->cells[g][i], "ts-limit-on");
                gtk_widget_add_css_class(d->cells[g][i], "ts-limit-past");
            } else {
                gtk_label_set_text(GTK_LABEL(d->cells[g][i]), "-");
                gtk_widget_remove_css_class(d->cells[g][i], "ts-limit-on");
                gtk_widget_remove_css_class(d->cells[g][i], "ts-limit-past");
            }
        }
        if (d->lbl_raw[g]) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "now=0x%08llX  accum=0x%04llX",
                     (unsigned long long)raws[g],
                     (unsigned long long)d->accum[g]);
            gtk_label_set_text(GTK_LABEL(d->lbl_raw[g]), buf);
        }
    }

    if (d->lbl_status) {
        int everCount = 0;
        for (int g = 0; g < 3; ++g)
            for (int i = 0; i < 13; ++i)
                if (d->counts[g][i] > 0) everCount++;

        char buf[192];
        const bool tvb = (d->accum[0] >> 14) & 1;
        const bool vmax = (d->accum[0] >> 15) & 1;
        snprintf(buf, sizeof(buf),
                 "当前 %d 项触发 / 累计 %d 项  |  TVB=%d VMAX=%d",
                 totalActive, everCount, (int)tvb, (int)vmax);
        gtk_label_set_text(GTK_LABEL(d->lbl_status), buf);
    }
}

static gboolean limits_timer(gpointer d) {
    LimitsDialog* dlg = (LimitsDialog*)d;

    if (!dlg || !dlg->alive) return G_SOURCE_REMOVE;
    limits_refresh(dlg);
    return G_SOURCE_CONTINUE;
}

GtkWidget* dlg_limit_reasons_new(GtkWindow* parent) {
    LimitsDialog* d = new LimitsDialog();
    memset(d, 0, sizeof(*d));

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), tr("Limit Reasons"));
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), S(560), S(470));
    d->win = win;

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(560), S(480));

    static const int kColX[3] = {158, 272, 386};

    {
        GtkWidget* tableframe = gtk_frame_new(NULL);
        gtk_widget_add_css_class(tableframe, "ts-red-group");
        put(fixed, tableframe, 4, 8, 540, 320);
    }

    for (int g = 0; g < 3; ++g) {
        GtkWidget* h = gtk_label_new(kGroupNames[g]);
        gtk_widget_add_css_class(h, "ts-limit-colhead");
        gtk_widget_set_halign(h, GTK_ALIGN_CENTER);
        put(fixed, h, kColX[g] - 30, 12, 90, 16);
    }

    static const int kVLineX[3] = {124, 240, 354};
    for (int g = 0; g < 3; ++g) {
        GtkWidget* vline = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
        gtk_widget_add_css_class(vline, "ts-limit-vline");
        put(fixed, vline, kVLineX[g], 30, 1, 296);
    }

    {
        GtkWidget* hline = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_add_css_class(hline, "ts-limit-hline");
        put(fixed, hline, 8, 30, 532, 1);
    }

    for (int i = 0; i < 13; ++i) {
        GtkWidget* hl = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_add_css_class(hl, "ts-limit-hline");
        put(fixed, hl, 8, 36 + i * 22 + 18, 532, 1);
    }

    for (int i = 0; i < 13; ++i) {
        const int y = 36 + i * 22;
        {
            GtkWidget* nl = gtk_label_new(kLimitRows[i].coreName);
            gtk_widget_set_halign(nl, GTK_ALIGN_START);
            gtk_label_set_xalign(GTK_LABEL(nl), 0.0f);
            put(fixed, nl, 12, y + 2, 118, 16);
        }
        for (int g = 0; g < 3; ++g) {
            d->cells[g][i] = gtk_label_new("-");
            gtk_widget_set_halign(d->cells[g][i], GTK_ALIGN_CENTER);
            put(fixed, d->cells[g][i], kColX[g] - 30, y + 2, 90, 16);
        }
    }

    for (int g = 0; g < 3; ++g) {
        d->lbl_raw[g] = gtk_label_new("");
        gtk_widget_add_css_class(d->lbl_raw[g], "ts-topo");
        gtk_widget_set_halign(d->lbl_raw[g], GTK_ALIGN_START);
        put(fixed, d->lbl_raw[g], 8, 340 + g * 18, 540, 16);
    }

    d->lbl_status = gtk_label_new("");
    gtk_widget_add_css_class(d->lbl_status, "ts-topo");
    gtk_widget_set_halign(d->lbl_status, GTK_ALIGN_START);
    put(fixed, d->lbl_status, 8, 400, 540, 20);

    GtkWidget* btn_clear = dlg_button("Clear");
    g_object_set_data(G_OBJECT(btn_clear), "dlgwin", win);
    g_object_set_data(G_OBJECT(btn_clear), "dlgwin", win);
    g_signal_connect(btn_clear, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer dd) {
            LimitsDialog* dlg = (LimitsDialog*)dd;
            uv::Controller* c = get_ctl();
            if (c) { try { c->clearLimitReasons(); } catch (...) {} }

            for (int i = 0; i < 3; ++i) {
                dlg->accum[i] = 0;
                dlg->last[i] = 0;
                for (int k = 0; k < 12; ++k) dlg->counts[i][k] = 0;
            }
            limits_refresh(dlg);
        }), d);
    put(fixed, btn_clear, 200, 424, 90, 28);

    GtkWidget* btn_close = dlg_button("Close");
    g_object_set_data(G_OBJECT(btn_close), "dlgwin", win);
    g_object_set_data(G_OBJECT(btn_close), "dlgwin", win);
    g_signal_connect(btn_close, "clicked",
        G_CALLBACK(+[](GtkButton* b, gpointer) {
            GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
        }), NULL);
    put(fixed, btn_close, 310, 424, 90, 28);

    {
        GtkWidget* rootbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget* handle = gtk_window_handle_new();
        GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(bar, "ts-dragbar");
        gtk_widget_set_size_request(bar, -1, S(18));
        gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), bar);
        gtk_box_append(GTK_BOX(rootbox), handle);
        gtk_box_append(GTK_BOX(rootbox), fixed);
        gtk_window_set_child(GTK_WINDOW(win), rootbox);
    }

    limits_refresh(d);
    for (int i = 0; i < 3; ++i) d->accum[i] = 0;
    limits_refresh(d);

    d->alive = TRUE;
    d->timer_id = g_timeout_add(250, limits_timer, d);

    g_object_set_data_full(G_OBJECT(win), "limits-dialog", d,
                           +[](gpointer p) {
                               LimitsDialog* dlg = (LimitsDialog*)p;
                               dlg->alive = FALSE;
                               if (dlg->timer_id) {
                                   g_source_remove(dlg->timer_id);
                                   dlg->timer_id = 0;
                               }
                               delete dlg;
                           });
    return win;
}
