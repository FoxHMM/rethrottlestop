

#include "dialogs.h"
#include "i18n.h"

#include "../../include/undervolt.hpp"
#include "../../include/mchbar.hpp"
#include "../../include/settings.hpp"
#include "../../include/msr_daemon.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern double ts_scale(void);
#define S(v) ((int)((v) * ts_scale()))

namespace {
uv::Controller* fivr_ctl() {
    static uv::Controller* c = nullptr;
    if (!c) {

        uv::MsrClient::ensureDaemon();
        try { uv::MsrClient::ensureDaemon(); c = new uv::Controller(); } catch (...) {}
    }
    return c;
}
}

static GtkWidget* red_group(const char* title) {
    GtkWidget* f = gtk_frame_new(NULL);
    gtk_widget_add_css_class(f, "ts-red-group");
    GtkWidget* lbl = gtk_label_new(tr(title));
    gtk_widget_add_css_class(lbl, "ts-red-title");
    gtk_frame_set_label_widget(GTK_FRAME(f), lbl);
    return f;
}

static GtkWidget* ts_radio(const char* label, GtkWidget* group) {
    GtkWidget* rb;
    if (label) {
        rb = gtk_check_button_new_with_label(tr(label));
    } else {
        rb = gtk_check_button_new();
    }
    if (group) {
        gtk_check_button_set_group(GTK_CHECK_BUTTON(rb),
                                   GTK_CHECK_BUTTON(group));
    }
    return rb;
}

static GtkWidget* ts_edit(const char* text) {
    GtkWidget* e = gtk_entry_new();
    if (text) gtk_editable_set_text(GTK_EDITABLE(e), text);
    gtk_widget_add_css_class(e, "ts-edit");
    gtk_widget_set_halign(e, GTK_ALIGN_FILL);
    return e;
}

static GtkWidget* ts_locked_edit(const char* text) {
    GtkWidget* e = gtk_entry_new();
    if (text) gtk_editable_set_text(GTK_EDITABLE(e), text);
    gtk_editable_set_editable(GTK_EDITABLE(e), FALSE);
    gtk_widget_set_can_focus(e, FALSE);
    gtk_widget_add_css_class(e, "ts-locked");
    return e;
}

static GtkWidget* ts_slider(double min, double max, double step) {
    GtkWidget* s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, step);
    gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
    gtk_widget_add_css_class(s, "ts-slider");

    gtk_widget_set_size_request(s, 200, 20);
    gtk_widget_set_hexpand(s, TRUE);
    gtk_widget_set_vexpand(s, FALSE);
    gtk_range_set_round_digits(GTK_RANGE(s), 1);

    return s;
}

#define GTK_SAFE_SET_TEXT(w, txt)                                        \
    do {                                                                 \
        if ((w) && GTK_IS_EDITABLE(w))                                   \
            gtk_editable_set_text(GTK_EDITABLE(w), (txt));               \
    } while (0)

static void put2(GtkWidget* fixed, GtkWidget* w, int x, int y, int w_, int h_) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    const int sw = S(w_) > 0 ? S(w_) : 1;
    const int sh = S(h_) > 0 ? S(h_) : 1;

    if (!GTK_IS_SCALE(w)) {
        gtk_widget_set_size_request(w, sw, sh);
    } else {
        gtk_widget_set_size_request(w, sw > 200 ? sw : 200, 20);
    }
}

typedef struct {

    GtkWidget* internet[4];
    GtkWidget* edit_extra_voltage;
    GtkWidget* chk_lock;
    GtkWidget* edit_ntr1;
    GtkWidget* edit_ntr2;
    GtkWidget* perf_cores[2];
    GtkWidget* edit_group_ratio[8];
    GtkWidget* spin_group_ratio[8];
    GtkWidget* btn_allcore[4];
    GtkWidget* edit_group_core[8];
    GtkWidget* edit_allcore[4];

    GtkWidget* radio_domain[6];
    GtkWidget* chk_unlock;
    GtkWidget* radio_adaptive;
    GtkWidget* radio_static;
    GtkWidget* edit_voltage;
    GtkWidget* btn_voltage_default;
    GtkWidget* scale_voltage;
    GtkWidget* edit_offset;
    GtkWidget* scale_offset;
    GtkWidget* radio_range[3];
    GtkWidget* edit_vccin;
    GtkWidget* scale_vccin;
    GtkWidget* radio_vccin_range[3];
    GtkWidget* edit_iccmax;
    GtkWidget* lbl_iccmax;
    GtkWidget* lbl_mem_ddr;
    GtkWidget* lbl_ucode;
    GtkWidget* lbl_status;
    GtkWidget* combo_range;
    GtkWidget* frame_misc;
    GtkWidget* scale_iccmax;
    GtkWidget* edit_cache_ratio;
    GtkWidget* scale_cache_ratio;

    GtkWidget* tbl_offset[6];
    GtkWidget* tbl_mode[6];
    GtkWidget* tbl_voltage[6];
    GtkWidget* edit_pl4;
    GtkWidget* edit_voltage_id;
    GtkWidget* edit_cache_ratio_val;
    GtkWidget* edit_epp_val;
    GtkWidget* edit_min;
    GtkWidget* edit_max;
    GtkWidget* edit_mem_ddr;
    GtkWidget* edit_ucode;
    GtkWidget* chk_avx;
    GtkWidget* chk_tvb;
    GtkWidget* chk_ring_down;
    GtkWidget* chk_mvboost;
    GtkWidget* chk_vmax;
    GtkWidget* chk_overclock;
    GtkWidget* chk_sleep_cache;
    GtkWidget* chk_sleep_voltage;
    GtkWidget* edit_avx;
    GtkWidget* edit_mvboost;
    GtkWidget* apply_btn;
    GtkWidget* edit_ntr_lock;
    GtkWidget* chk_unlimited;
    GtkWidget* edit_overclock;
    GtkWidget* edit_extra_voltage2;
    GtkWidget* radio_volt_mode[2];
    GtkWidget* save_opts[3];

    int domain;
    gboolean updating;
    gboolean hasPendingEdit;
    int saveMode;
    bool unlocked = false;
    int activeProfile = 0;
    bool ratiosInited = false;
    int coreType = 0;
} FivrState;

static FivrState g_fivr;
static gboolean g_fivrAlive = FALSE;
static guint g_fivrTimerId = 0;

static int table_row_to_plane(int row) {

    switch (row) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 2;
        default: return -1;
    }
}

static void fivr_refresh_offset_table(void) {
    if (!g_fivrAlive) return;
    uv::Controller* c = fivr_ctl();
    if (!c) return;

    char buf[24];
    for (int row = 0; row < 6; ++row) {
        const int plane = table_row_to_plane(row);

        if (g_fivr.tbl_offset[row]) {
            std::optional<double> mv;
            if (plane >= 0) {
                mv = uv::MsrClient::readOffset(plane);
            } else {

                mv = uv::MsrClient::readOffset(3);
            }

            if (!mv) {
                gtk_label_set_text(GTK_LABEL(g_fivr.tbl_offset[row]), "-");
                gtk_widget_remove_css_class(g_fivr.tbl_offset[row], "ts-untrusted");
            } else {

                snprintf(buf, sizeof(buf), "%+.2f", *mv);
                gtk_label_set_text(GTK_LABEL(g_fivr.tbl_offset[row]), buf);
                gtk_widget_remove_css_class(g_fivr.tbl_offset[row], "ts-untrusted");
            }
        }

        if (g_fivr.tbl_voltage[row]) {
            std::optional<double> v = c->readDomainVoltage(row);
            if (v) {
                snprintf(buf, sizeof(buf), "%.4f", *v);
                gtk_label_set_text(GTK_LABEL(g_fivr.tbl_voltage[row]), buf);
            } else {
                gtk_label_set_text(GTK_LABEL(g_fivr.tbl_voltage[row]), "Default");
            }
        }

        if (g_fivr.tbl_mode[row]) {
            const bool adaptive = g_fivr.radio_adaptive
                ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.radio_adaptive))
                : true;
            gtk_label_set_text(GTK_LABEL(g_fivr.tbl_mode[row]),
                               adaptive ? "Adaptive" : "Static");
        }
    }
}

static gboolean fivr_table_timer(gpointer data) {
    if (!g_fivrAlive) return G_SOURCE_REMOVE;
    fivr_refresh_offset_table();
    return G_SOURCE_CONTINUE;
}

static double g_domainBuffer[6] = {0, 0, 0, 0, 0, 0};
static bool   g_domainHasBuffer[6] = {false, false, false, false, false, false};

static int domain_to_plane(int dom) {

    switch (dom) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 2;
        default: return 0;
    }
}

static void fivr_sync_domain(void) {
    if (!g_fivrAlive) return;
    uv::Controller* c = fivr_ctl();
    if (!c) return;

    const int plane = domain_to_plane(g_fivr.domain);
    char buf[32];
    g_fivr.updating = TRUE;

    double mv = 0;
    bool haveVal = false;
    if (plane >= 0) {
        try {
            mv = c->readOffset(static_cast<uv::Plane>(plane));
            haveVal = true;
        } catch (...) {}
    } else {

        if (auto v = c->readDomainVoltage(g_fivr.domain)) {
            mv = *v * 1000.0;
            haveVal = true;
        }
    }

    if (g_domainHasBuffer[g_fivr.domain]) {
        mv = g_domainBuffer[g_fivr.domain];
        haveVal = true;
    }

    if (haveVal) {
        if (g_fivr.scale_offset && GTK_IS_RANGE(g_fivr.scale_offset)) {
            double sv = mv;
            if (sv < -1000.0) sv = -1000.0;
            if (sv > 1000.0) sv = 1000.0;
            gtk_range_set_value(GTK_RANGE(g_fivr.scale_offset), sv);
        }
        snprintf(buf, sizeof(buf), "%.1f", mv);
        GTK_SAFE_SET_TEXT(g_fivr.edit_offset, buf);
    } else {
        GTK_SAFE_SET_TEXT(g_fivr.edit_offset, "-");
    }

    if (g_fivr.edit_voltage) {
        if (auto fv = c->readFreqVid()) {
            snprintf(buf, sizeof(buf), "%.4f", fv->vid);
            GTK_SAFE_SET_TEXT(g_fivr.edit_voltage, buf);
        } else {
            GTK_SAFE_SET_TEXT(g_fivr.edit_voltage, "-");
        }
    }

    if (g_fivr.edit_iccmax) {
        std::optional<double> icc;
        if (plane >= 0) {
            try { icc = c->readIccMax(static_cast<uv::Plane>(plane)); }
            catch (...) {}
        }
        if (icc) {
            snprintf(buf, sizeof(buf), "%.2f", *icc);
            GTK_SAFE_SET_TEXT(g_fivr.edit_iccmax, buf);
        } else {

            GTK_SAFE_SET_TEXT(g_fivr.edit_iccmax, "N/A");
        }
    }

    if (g_fivr.edit_cache_ratio) {
        const auto ring = c->readRingRatio();
        if (ring.ok) {
            snprintf(buf, sizeof(buf), "%d", ring.maxRing);
            GTK_SAFE_SET_TEXT(g_fivr.edit_cache_ratio, buf);
        } else {
            GTK_SAFE_SET_TEXT(g_fivr.edit_cache_ratio, "-");
        }
    }

    const bool unlocked = g_fivr.unlocked;
    if (g_fivr.scale_offset) gtk_widget_set_sensitive(g_fivr.scale_offset, unlocked);
    if (g_fivr.edit_offset) gtk_widget_set_sensitive(g_fivr.edit_offset, unlocked);

    if (g_fivr.radio_adaptive)
        gtk_widget_set_sensitive(g_fivr.radio_adaptive, unlocked);
    if (g_fivr.radio_static)
        gtk_widget_set_sensitive(g_fivr.radio_static, unlocked);

    g_fivr.updating = FALSE;
}

static void fivr_switch_core_group(int type);
extern "C" void fivr_load_real_data(void);
static void fivr_save_config(void);
static void fivr_refresh_offset_table(void);
static void fivr_apply_all(void);

static void build_left_column(GtkWidget* fixed) {

    GtkWidget* g1 = red_group("Profile");
    put2(fixed, g1, 14, 40, 262, 52);
    {
        const auto st = uv::loadSettings();
        static const char* kDefaultNames[4] = {"Profile 1", "Profile 2",
                                               "Profile 3", "Profile 4"};
        for (int i = 0; i < 4; ++i) {
            const char* nm = kDefaultNames[i];
            if (st.valid && !st.mainwin.profileName[i].empty()) {
                nm = st.mainwin.profileName[i].c_str();
            }
            g_fivr.internet[i] = ts_radio(nm, i ? g_fivr.internet[0] : NULL);
            g_object_set_data(G_OBJECT(g_fivr.internet[i]), "idx",
                              GINT_TO_POINTER(i));
            put2(fixed, g_fivr.internet[i], 26 + i * 62, 58, 60, 16);
            g_signal_connect(g_fivr.internet[i], "toggled",
                G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
                    if (g_fivr.updating) return;
                    if (!gtk_check_button_get_active(b)) return;

                    g_fivr.activeProfile = GPOINTER_TO_INT(
                        g_object_get_data(G_OBJECT(b), "idx"));
                    auto st2 = uv::loadSettings();
                    st2.mainwin.activeProfile = g_fivr.activeProfile;
                    uv::saveSettings(st2);
                    fivr_load_real_data();
                }), NULL);
        }

        const int cur = (st.valid && st.mainwin.activeProfile >= 0 &&
                         st.mainwin.activeProfile < 4)
                        ? st.mainwin.activeProfile : 0;
        g_fivr.activeProfile = cur;
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.internet[cur]), TRUE);
    }

    GtkWidget* g2 = red_group("Extra Turbo Voltage");
    put2(fixed, g2, 14, 100, 262, 62);
    put2(fixed, gtk_label_new("Extra Voltage"), 26, 122, 90, 14);
    g_fivr.edit_extra_voltage = ts_edit("");
    put2(fixed, g_fivr.edit_extra_voltage, 150, 120, 52, 16);

    GtkWidget* g3 = red_group("Non Turbo Ratio");
    put2(fixed, g3, 14, 170, 262, 66);
    g_fivr.chk_lock = gtk_check_button_new_with_label("Lock");
    put2(fixed, g_fivr.chk_lock, 24, 192, 50, 14);
    g_fivr.edit_ntr1 = ts_locked_edit("");
    put2(fixed, g_fivr.edit_ntr1, 110, 190, 52, 16);
    g_fivr.edit_ntr2 = ts_locked_edit("");
    put2(fixed, g_fivr.edit_ntr2, 172, 190, 52, 16);

    GtkWidget* g4 = red_group("Turbo Overclocking");
    put2(fixed, g4, 14, 244, 262, 62);
    put2(fixed, gtk_label_new("Max Ratio"), 26, 268, 80, 14);
    g_fivr.edit_overclock = ts_edit("");
    put2(fixed, g_fivr.edit_overclock, 116, 266, 56, 18);

    GtkWidget* g5 = red_group("Performance Cores");
    put2(fixed, g5, 14, 314, 262, 58);

    static const char* kCoreNames[2] = {"P Core", "E Core"};
    for (int i = 0; i < 2; ++i) {
        g_fivr.perf_cores[i] = ts_radio(kCoreNames[i],
                                        i ? g_fivr.perf_cores[0] : NULL);
        g_object_set_data(G_OBJECT(g_fivr.perf_cores[i]), "type",
                          GINT_TO_POINTER(i));
        g_signal_connect(g_fivr.perf_cores[i], "toggled",
            G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
                if (g_fivr.updating) return;
                if (!gtk_check_button_get_active(b)) return;
                fivr_switch_core_group(
                    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "type")));
            }), NULL);
        put2(fixed, g_fivr.perf_cores[i], 40 + i * 120, 334, 100, 16);
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.perf_cores[0]), TRUE);

    GtkWidget* g6 = red_group("Turbo Groups");
    put2(fixed, g6, 14, 380, 262, 248);

    {
        GtkWidget* h1 = gtk_label_new("Ratios");
        gtk_label_set_xalign(GTK_LABEL(h1), 0.5f);
        put2(fixed, h1, 62, 398, 44, 12);
        GtkWidget* h2 = gtk_label_new("Cores");
        gtk_label_set_xalign(GTK_LABEL(h2), 0.5f);
        put2(fixed, h2, 112, 398, 40, 12);
    }

    for (int i = 0; i < 8; ++i) {
        const int ry = 418 + i * 25;

        char lbl[16];
        snprintf(lbl, sizeof(lbl), "Group %d", i);
        put2(fixed, gtk_label_new(lbl), 18, ry + 2, 42, 14);

        g_fivr.edit_group_ratio[i] = ts_edit("0");
        put2(fixed, g_fivr.edit_group_ratio[i], 62, ry, 44, 18);

        char dv[8];
        snprintf(dv, sizeof(dv), "%d", i + 1);
        g_fivr.edit_group_core[i] = ts_locked_edit(dv);
        put2(fixed, g_fivr.edit_group_core[i], 112, ry, 40, 18);

        GtkWidget* up = gtk_button_new_with_label("▲");
        gtk_widget_add_css_class(up, "ts-udbtn");
        g_object_set_data(G_OBJECT(up), "row", GINT_TO_POINTER(i));

        GtkWidget* dn = gtk_button_new_with_label("▼");
        gtk_widget_add_css_class(dn, "ts-udbtn");
        g_object_set_data(G_OBJECT(dn), "row", GINT_TO_POINTER(i));

        g_signal_connect(up, "clicked",
            G_CALLBACK(+[](GtkButton* b, gpointer) {
                const int row = GPOINTER_TO_INT(
                    g_object_get_data(G_OBJECT(b), "row"));
                if (!g_fivr.edit_group_ratio[row]) return;
                const int v = atoi(gtk_editable_get_text(
                    GTK_EDITABLE(g_fivr.edit_group_ratio[row])));
                int nv = (v >= 255) ? 255 : v + 1;
                char b2[8]; snprintf(b2, sizeof(b2), "%d", nv);
                gtk_editable_set_text(
                    GTK_EDITABLE(g_fivr.edit_group_ratio[row]), b2);
            }), NULL);
        g_signal_connect(dn, "clicked",
            G_CALLBACK(+[](GtkButton* b, gpointer) {
                const int row = GPOINTER_TO_INT(
                    g_object_get_data(G_OBJECT(b), "row"));
                if (!g_fivr.edit_group_ratio[row]) return;
                const int v = atoi(gtk_editable_get_text(
                    GTK_EDITABLE(g_fivr.edit_group_ratio[row])));
                int nv = (v <= 0) ? 0 : v - 1;
                char b2[8]; snprintf(b2, sizeof(b2), "%d", nv);
                gtk_editable_set_text(
                    GTK_EDITABLE(g_fivr.edit_group_ratio[row]), b2);
            }), NULL);

        put2(fixed, up, 150, ry, 20, 18);
        put2(fixed, dn, 172, ry, 20, 18);
    }

    GtkWidget* g7 = red_group("All Core");
    put2(fixed, g7, 14, 636, 262, 54);
    for (int i = 0; i < 4; ++i) {
        g_fivr.btn_allcore[i] = gtk_button_new_with_label("-");
        g_object_set_data(G_OBJECT(g_fivr.btn_allcore[i]), "idx",
                          GINT_TO_POINTER(i));
        gtk_widget_add_css_class(g_fivr.btn_allcore[i], "ts-btn");
        g_object_set_data(G_OBJECT(g_fivr.btn_allcore[i]), "idx",
                          GINT_TO_POINTER(i));
        g_signal_connect(g_fivr.btn_allcore[i], "clicked",
            G_CALLBACK(+[](GtkButton* b, gpointer) {

                const int idx = GPOINTER_TO_INT(
                    g_object_get_data(G_OBJECT(b), "idx"));
                const char* txt = gtk_button_get_label(b);
                if (!txt || !*txt || strcmp(txt, "-") == 0) return;
                const int v = atoi(txt);
                if (v <= 0) return;
                for (int g = 0; g < 8; ++g) {
                    if (g_fivr.edit_group_ratio[g]) {
                        char b2[8];
                        snprintf(b2, sizeof(b2), "%d", v);
                        gtk_editable_set_text(
                            GTK_EDITABLE(g_fivr.edit_group_ratio[g]), b2);
                    }
                }
            }), NULL);
        put2(fixed, g_fivr.btn_allcore[i], 30 + i * 62, 652, 50, 20);
    }
}

static void build_center_column(GtkWidget* fixed) {

    GtkWidget* g1 = red_group("FIVR Control");
    put2(fixed, g1, 302, 40, 310, 100);

    const char* doms[] = {"CPU Core", "Intel GPU", "CPU P Cache",
                          "iGPU Unslice", "System Agent", "CPU E Cache"};
    for (int i = 0; i < 6; ++i) {
        GtkWidget* rb = ts_radio(doms[i], i ? g_fivr.radio_domain[0] : NULL);
        g_object_set_data(G_OBJECT(rb), "idx", GINT_TO_POINTER(i));
        const int col = i % 2, row = i / 2;
        put2(fixed, rb, 316 + col * 150, 62 + row * 22, 140, 16);
        g_signal_connect(rb, "toggled", G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
            if (!gtk_check_button_get_active(b)) return;
            g_fivr.domain = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "idx"));
            fivr_sync_domain();
        }), NULL);
        g_fivr.radio_domain[i] = rb;
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.radio_domain[0]), TRUE);

    GtkWidget* g2 = red_group("CPU Core");
    put2(fixed, g2, 302, 148, 310, 420);

    g_fivr.chk_unlock = gtk_check_button_new_with_label("Unlock Adjustable Voltage");
    put2(fixed, g_fivr.chk_unlock, 316, 164, 200, 18);

    g_signal_connect(g_fivr.chk_unlock, "toggled",
        G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
            if (g_fivr.updating) return;
            g_fivr.unlocked = gtk_check_button_get_active(b);

            if (g_fivr.unlocked) {
                uv::Controller* c = fivr_ctl();
                if (c) c->unlockVoltage();
            }
            fivr_sync_domain();
        }), NULL);

    g_fivr.radio_adaptive = ts_radio("Adaptive", NULL);
    put2(fixed, g_fivr.radio_adaptive, 316, 188, 90, 16);
    g_fivr.radio_static = ts_radio("Static", g_fivr.radio_adaptive);
    put2(fixed, g_fivr.radio_static, 420, 188, 80, 16);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.radio_adaptive), TRUE);

    put2(fixed, gtk_label_new("Voltage"), 316, 216, 60, 14);
    g_fivr.btn_voltage_default = gtk_button_new_with_label("Default");
    put2(fixed, g_fivr.btn_voltage_default, 420, 212, 76, 20);
    put2(fixed, gtk_label_new("V"), 560, 216, 20, 14);
    g_fivr.scale_voltage = ts_slider(0, 2, 0.001);
    put2(fixed, g_fivr.scale_voltage, 316, 236, 240, 18);

    put2(fixed, gtk_label_new("Offset Voltage"), 316, 268, 100, 14);
    g_fivr.edit_offset = ts_edit("");
    put2(fixed, g_fivr.edit_offset, 420, 264, 76, 20);

    {
        GtkEventController* ec = gtk_event_controller_focus_new();
        g_signal_connect(ec, "leave",
            G_CALLBACK(+[](GtkEventController*, gpointer) {
                if (g_fivr.updating) return;
                const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_offset));
                if (!t || !*t) return;
                char* end = nullptr;
                const double mv = strtod(t, &end);
                if (end == t) return;

                double clamped = mv;
                if (clamped > 0.0) clamped = 0.0;
                if (clamped < -2000.0) clamped = -2000.0;

                const int16_t raw = uv::encodeOffsetRaw(clamped);
                const double actual = uv::decodeOffsetRaw(raw);
                char b[32];
                snprintf(b, sizeof(b), "%.1f", actual);

                g_fivr.updating = TRUE;
                if (g_fivr.edit_offset &&
                    GTK_IS_EDITABLE(g_fivr.edit_offset)) {
                    GTK_SAFE_SET_TEXT(g_fivr.edit_offset, b);
                }
                if (g_fivr.scale_offset &&
                    GTK_IS_RANGE(g_fivr.scale_offset)) {

                    double sv = actual;
                    if (sv < -1000.0) sv = -1000.0;
                    if (sv > 1000.0) sv = 1000.0;
                    gtk_range_set_value(GTK_RANGE(g_fivr.scale_offset), sv);
                }
                g_fivr.updating = FALSE;
            }), NULL);
        gtk_widget_add_controller(g_fivr.edit_offset, ec);
    }
    put2(fixed, gtk_label_new("mV"), 508, 268, 30, 14);
    g_fivr.scale_offset = ts_slider(-1000, 1000, 0.1);
    put2(fixed, g_fivr.scale_offset, 316, 290, 240, 18);
    g_signal_connect(g_fivr.scale_offset, "value-changed",
        G_CALLBACK(+[](GtkRange* r, gpointer) {
            if (g_fivr.updating) return;
            if (!g_fivr.edit_offset || !GTK_IS_EDITABLE(g_fivr.edit_offset)) return;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f", gtk_range_get_value(r));
            GTK_SAFE_SET_TEXT(g_fivr.edit_offset, buf);
        }), NULL);

    g_signal_connect(g_fivr.edit_offset, "changed",
        G_CALLBACK(+[](GtkEditable* e, gpointer) {
            if (g_fivr.updating) return;
            const double v = atof(gtk_editable_get_text(e));
            if (g_fivr.domain >= 0 && g_fivr.domain < 6) {
                g_domainBuffer[g_fivr.domain] = v;
                g_domainHasBuffer[g_fivr.domain] = true;
            }
        }), NULL);

    put2(fixed, gtk_label_new("Range"), 316, 320, 46, 14);
    const char* ranges[] = {"125 mV", "250 mV", "1000 mV"};
    for (int i = 0; i < 3; ++i) {
        GtkWidget* rb = ts_radio(ranges[i], i ? g_fivr.radio_range[0] : NULL);
        put2(fixed, rb, 364 + i * 78, 318, 72, 16);
        g_fivr.radio_range[i] = rb;
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.radio_range[0]), TRUE);

    put2(fixed, gtk_label_new("VCCIN"), 316, 350, 60, 14);
    g_fivr.edit_vccin = ts_edit("");
    put2(fixed, g_fivr.edit_vccin, 420, 346, 76, 20);
    put2(fixed, gtk_label_new("V"), 560, 350, 20, 14);
    g_fivr.scale_vccin = ts_slider(0, 2.5, 0.01);
    put2(fixed, g_fivr.scale_vccin, 316, 372, 240, 18);
    put2(fixed, gtk_label_new("Range"), 316, 400, 46, 14);
    const char* vr[] = {"1.80 V", "2.00 V", "2.30 V"};
    for (int i = 0; i < 3; ++i) {
        GtkWidget* rb = ts_radio(vr[i], i ? g_fivr.radio_vccin_range[0] : NULL);
        put2(fixed, rb, 364 + i * 72, 398, 70, 16);
        g_fivr.radio_vccin_range[i] = rb;
    }

    g_fivr.lbl_iccmax = gtk_label_new("IccMax");
    put2(fixed, g_fivr.lbl_iccmax, 316, 430, 60, 14);
    g_fivr.edit_iccmax = ts_edit("N/A");
    put2(fixed, g_fivr.edit_iccmax, 420, 426, 76, 20);
    put2(fixed, gtk_label_new("A"), 508, 430, 20, 14);
    g_fivr.scale_iccmax = ts_slider(0, 600, 0.25);
    put2(fixed, g_fivr.scale_iccmax, 316, 452, 240, 18);

    put2(fixed, gtk_label_new("Cache Ratio"), 316, 484, 80, 14);
    g_fivr.edit_cache_ratio = ts_edit("");
    put2(fixed, g_fivr.edit_cache_ratio, 420, 480, 76, 20);
    g_fivr.scale_cache_ratio = ts_slider(0, 64, 1);
    put2(fixed, g_fivr.scale_cache_ratio, 316, 504, 240, 18);
}

static void build_right_column(GtkWidget* fixed) {

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    const char* heads[] = {"Name", "Mode", "Voltage", "Offset"};
    for (int c = 0; c < 4; ++c) {
        GtkWidget* h = gtk_label_new(tr(heads[c]));
        gtk_widget_add_css_class(h, "ts-table-head");
        gtk_widget_set_hexpand(h, TRUE);
        gtk_grid_attach(GTK_GRID(grid), h, c, 0, 1, 1);
    }
    const char* rows[] = {"CPU Core", "Intel GPU", "CPU P Cache", "iGPU Unslice",
                          "System Agent", "CPU E Cache"};
    for (int r = 0; r < 6; ++r) {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(tr(rows[r])), 0, r + 1, 1, 1);

        g_fivr.tbl_mode[r] = gtk_label_new("Adaptive");
        gtk_grid_attach(GTK_GRID(grid), g_fivr.tbl_mode[r], 1, r + 1, 1, 1);

        g_fivr.tbl_voltage[r] = gtk_label_new("Default");
        gtk_grid_attach(GTK_GRID(grid), g_fivr.tbl_voltage[r], 2, r + 1, 1, 1);

        g_fivr.tbl_offset[r] = gtk_label_new("-");
        gtk_widget_set_halign(g_fivr.tbl_offset[r], GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(grid), g_fivr.tbl_offset[r], 3, r + 1, 1, 1);
    }

    const char* extraNames[] = {"PL4", "Voltage ID", "Cache Ratio", "Speed Shift EPP"};
    GtkWidget** extraVals[] = {
        &g_fivr.edit_pl4, &g_fivr.edit_voltage_id,
        &g_fivr.edit_cache_ratio_val, &g_fivr.edit_epp_val,
    };
    for (int r = 0; r < 4; ++r) {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(tr(extraNames[r])),
                        0, r + 7, 1, 1);
        GtkWidget* vl = gtk_label_new("-");
        gtk_widget_set_halign(vl, GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(grid), vl, 2, r + 7, 1, 1);
        *extraVals[r] = vl;
    }

    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_widget_set_halign(grid, GTK_ALIGN_FILL);
    gtk_widget_set_valign(grid, GTK_ALIGN_START);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_widget_add_css_class(scroll, "ts-table-frame");
    put2(fixed, scroll, 622, 40, 340, 300);

    GtkWidget* gcr = red_group("Cache Ratio");
    put2(fixed, gcr, 622, 350, 340, 130);
    put2(fixed, gtk_label_new("Min / Max"), 634, 375, 70, 14);
    g_fivr.edit_min = ts_edit("");
    put2(fixed, g_fivr.edit_min, 730, 372, 60, 18);
    g_fivr.edit_max = ts_edit("");
    put2(fixed, g_fivr.edit_max, 800, 372, 60, 18);
    g_fivr.lbl_mem_ddr = gtk_label_new("Memory DDR");
    put2(fixed, g_fivr.lbl_mem_ddr, 634, 400, 90, 14);
    g_fivr.edit_mem_ddr = ts_locked_edit("");
    put2(fixed, g_fivr.edit_mem_ddr, 730, 397, 60, 18);
    g_fivr.lbl_ucode = gtk_label_new("uCode -");
    put2(fixed, g_fivr.lbl_ucode, 800, 400, 70, 14);
    g_fivr.chk_sleep_cache = gtk_check_button_new_with_label("Cache Ratio");
    put2(fixed, g_fivr.chk_sleep_cache, 634, 428, 110, 14);
    g_fivr.chk_sleep_voltage = gtk_check_button_new_with_label("Voltage");
    put2(fixed, g_fivr.chk_sleep_voltage, 750, 428, 80, 14);

    GtkWidget* gmisc = red_group("Miscellaneous");
    g_fivr.frame_misc = gmisc;
    put2(fixed, gmisc, 622, 490, 340, 140);
    g_fivr.chk_avx = gtk_check_button_new_with_label("AVX Offset");

    g_signal_connect(g_fivr.chk_avx, "toggled",
        G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
            if (g_fivr.updating) return;
            g_fivr.hasPendingEdit = TRUE;
            if (g_fivr.apply_btn) gtk_widget_set_sensitive(g_fivr.apply_btn, TRUE);
        }), NULL);
    put2(fixed, g_fivr.chk_avx, 634, 510, 110, 14);
    g_fivr.edit_avx = ts_edit("-");
    put2(fixed, g_fivr.edit_avx, 800, 508, 56, 16);
    g_fivr.chk_tvb = gtk_check_button_new_with_label("TVB");

    g_signal_connect(g_fivr.chk_tvb, "toggled",
        G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
            if (g_fivr.updating) return;
            g_fivr.hasPendingEdit = TRUE;
            if (g_fivr.apply_btn) gtk_widget_set_sensitive(g_fivr.apply_btn, TRUE);
        }), NULL);
    put2(fixed, g_fivr.chk_tvb, 634, 532, 170, 14);
    g_fivr.chk_vmax = gtk_check_button_new_with_label("V-Max Stress");
    put2(fixed, g_fivr.chk_vmax, 810, 532, 110, 14);
    g_fivr.chk_ring_down = gtk_check_button_new_with_label("Ring Down Bin");

    g_signal_connect(g_fivr.chk_ring_down, "toggled",
        G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
            if (g_fivr.updating) return;
            g_fivr.hasPendingEdit = TRUE;
            if (g_fivr.apply_btn) gtk_widget_set_sensitive(g_fivr.apply_btn, TRUE);
        }), NULL);
    put2(fixed, g_fivr.chk_ring_down, 634, 554, 130, 14);
    g_fivr.chk_overclock = gtk_check_button_new_with_label("Overclock");
    put2(fixed, g_fivr.chk_overclock, 810, 554, 100, 14);
    g_fivr.chk_mvboost = gtk_check_button_new_with_label("mV Boost");
    put2(fixed, g_fivr.chk_mvboost, 634, 576, 160, 14);
    g_fivr.edit_mvboost = ts_edit("-");
    put2(fixed, g_fivr.edit_mvboost, 800, 574, 56, 16);

    GtkWidget* gsave = red_group("Save Voltage Changes");
    put2(fixed, gsave, 622, 632, 340, 66);
    const char* opts[] = {
        "OK - Do not save voltages.",
        "OK - Save voltages after exit.",
        "OK - Save voltages immediately.",
    };
    for (int i = 0; i < 3; ++i) {
        GtkWidget* rb = ts_radio(opts[i], i ? g_fivr.save_opts[0] : NULL);
        g_object_set_data(G_OBJECT(rb), "mode", GINT_TO_POINTER(i));
        g_signal_connect(rb, "toggled",
            G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
                if (!gtk_check_button_get_active(b)) return;
                g_fivr.saveMode = GPOINTER_TO_INT(
                    g_object_get_data(G_OBJECT(b), "mode"));
            }), NULL);
        put2(fixed, rb, 634, 645 + i * 17, 290, 14);
        g_fivr.save_opts[i] = rb;
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.save_opts[0]), TRUE);
}

static void build_bottom_buttons(GtkWidget* fixed) {

    GtkWidget* defaults = gtk_button_new_with_label("Defaults");
    put2(fixed, defaults, 20, 706, 100, 24);
    g_signal_connect(defaults, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer) {
            g_fivr.updating = TRUE;

            if (g_fivr.edit_offset && GTK_IS_EDITABLE(g_fivr.edit_offset))
                GTK_SAFE_SET_TEXT(g_fivr.edit_offset, "0.0");
            if (g_fivr.scale_offset && GTK_IS_RANGE(g_fivr.scale_offset))
                gtk_range_set_value(GTK_RANGE(g_fivr.scale_offset), 0.0);

            if (g_fivr.radio_adaptive)
                gtk_check_button_set_active(
                    GTK_CHECK_BUTTON(g_fivr.radio_adaptive), TRUE);

            if (g_fivr.combo_range)
                gtk_drop_down_set_selected(GTK_DROP_DOWN(g_fivr.combo_range), 0);

            for (int i = 0; i < 6; ++i) {
                g_domainHasBuffer[i] = false;
                g_domainBuffer[i] = 0;
            }
            g_fivr.updating = FALSE;
        }), NULL);

    GtkWidget* reset = gtk_button_new_with_label("Reset Voltage");
    put2(fixed, reset, 128, 706, 120, 24);
    g_signal_connect(reset, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer) {

            for (int i = 0; i < 6; ++i) {
                g_domainHasBuffer[i] = false;
                g_domainBuffer[i] = 0;
            }

            fivr_load_real_data();
            fivr_sync_domain();
        }), NULL);

    GtkWidget* zero = gtk_button_new_with_label("Zero Offset");
    put2(fixed, zero, 256, 706, 120, 24);
    g_signal_connect(zero, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer) {
            uv::Controller* c = fivr_ctl();
            if (!c) return;
            const bool ok = c->zeroAllOffsets();
            if (g_fivr.lbl_status) {
                gtk_label_set_text(GTK_LABEL(g_fivr.lbl_status),
                    ok ? "Zero Offset: 全部平面已清零"
                       : "Zero Offset: 部分平面清零失败");
            }
            fivr_load_real_data();
            fivr_sync_domain();
        }), NULL);
    GtkWidget* ok = gtk_button_new_with_label("OK");
    gtk_widget_add_css_class(ok, "ts-btn");
    put2(fixed, ok, 384, 706, 90, 24);
    g_signal_connect(ok, "clicked",
        G_CALLBACK(+[](GtkButton* b, gpointer) {

            fivr_apply_all();
            GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(b));
            if (GTK_IS_WINDOW(root)) gtk_window_destroy(GTK_WINDOW(root));
        }), NULL);
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    gtk_widget_add_css_class(cancel, "ts-btn");

    g_object_set_data(G_OBJECT(cancel), "dlgroot", fixed);
    put2(fixed, cancel, 482, 706, 90, 24);
    g_signal_connect(cancel, "clicked",
        G_CALLBACK(+[](GtkButton* b, gpointer) {

            for (int i = 0; i < 6; ++i) {
                g_domainHasBuffer[i] = false;
                g_domainBuffer[i] = 0;
            }
            fivr_load_real_data();
            GtkWidget* fx = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgroot"));
            GtkRoot* rr = fx ? gtk_widget_get_root(fx) : NULL;
            if (rr && GTK_IS_WINDOW(rr)) gtk_window_destroy(GTK_WINDOW(rr));
        }), NULL);
    GtkWidget* vfbtn = gtk_button_new_with_label("V/F Point");
    gtk_widget_add_css_class(vfbtn, "ts-btn");
    put2(fixed, vfbtn, 810, 706, 100, 24);
    g_signal_connect(vfbtn, "clicked",
        G_CALLBACK(+[](GtkButton* b, gpointer) {
            GtkRoot* r = gtk_widget_get_root(GTK_WIDGET(b));
            GtkWindow* parent = GTK_IS_WINDOW(r) ? GTK_WINDOW(r) : NULL;
            GtkWidget* dlg = dlg_vf_point_new(parent);
            gtk_window_present(GTK_WINDOW(dlg));
        }), NULL);

    GtkWidget* apply = gtk_button_new_with_label("Apply");
    gtk_widget_set_sensitive(apply, FALSE);
    put2(fixed, apply, 580, 706, 90, 24);
    g_fivr.apply_btn = apply;

    auto enableApply = +[](GtkWidget*, gpointer d) {

        if (g_fivr.updating) return;
        if (d && GTK_IS_WIDGET(d))
            gtk_widget_set_sensitive(GTK_WIDGET(d), TRUE);
    };
    {
        GtkWidget* ctrls[] = {
            g_fivr.scale_offset, g_fivr.edit_offset,
            g_fivr.edit_voltage, g_fivr.scale_voltage,
            g_fivr.edit_iccmax, g_fivr.scale_iccmax,
            g_fivr.edit_cache_ratio, g_fivr.scale_cache_ratio,
            g_fivr.edit_avx, g_fivr.edit_mvboost,
            g_fivr.chk_tvb, g_fivr.chk_ring_down,
            g_fivr.chk_vmax, g_fivr.chk_mvboost, g_fivr.chk_overclock,
            g_fivr.edit_overclock, g_fivr.chk_lock, g_fivr.chk_unlimited,
            g_fivr.edit_ntr1, g_fivr.edit_ntr2, g_fivr.edit_extra_voltage,
        };
        for (GtkWidget* w : ctrls) {
            if (!w) continue;
            if (GTK_IS_EDITABLE(w))
                g_signal_connect(w, "changed", G_CALLBACK(enableApply), apply);
            else if (GTK_IS_RANGE(w))
                g_signal_connect(w, "value-changed", G_CALLBACK(enableApply), apply);
            else if (GTK_IS_CHECK_BUTTON(w))
                g_signal_connect(w, "toggled", G_CALLBACK(enableApply), apply);
        }

        for (int i = 0; i < 8; ++i) {
            if (g_fivr.edit_group_ratio[i])
                g_signal_connect(g_fivr.edit_group_ratio[i], "changed",
                                 G_CALLBACK(enableApply), apply);
        }
        for (int i = 0; i < 4; ++i) {
            if (g_fivr.edit_allcore[i])
                g_signal_connect(g_fivr.edit_allcore[i], "changed",
                                 G_CALLBACK(enableApply), apply);
        }
    }

    g_signal_connect(reset, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        fivr_sync_domain();
    }), NULL);
    g_signal_connect(zero, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        uv::Controller* c = fivr_ctl();
        if (!c) return;
        try {
            for (int i = 0; i < 5; ++i) c->setOffset(static_cast<uv::Plane>(i), 0.0, true);
            fivr_sync_domain();
        } catch (...) {}
    }), NULL);
    g_signal_connect(apply, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {

        fivr_apply_all();
        gtk_widget_set_sensitive(g_fivr.apply_btn, FALSE);

        if (g_fivr.apply_btn) {
            gtk_button_set_label(GTK_BUTTON(g_fivr.apply_btn), "Applied ✓");
            g_timeout_add(1500, +[](gpointer w) -> gboolean {
                if (w && GTK_IS_BUTTON(w))
                    gtk_button_set_label(GTK_BUTTON(w), "Apply");
                return G_SOURCE_REMOVE;
            }, g_fivr.apply_btn);
        }
        g_printerr("[apply] 已应用\n");
    }), NULL);
}

GtkWidget* dlg_fivr_control_new(GtkWindow* parent) {
    memset(&g_fivr, 0, sizeof(g_fivr));
    g_fivr.domain = 0;

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "FIVR Control");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), S(980), S(770));

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(980), S(742));

    build_left_column(fixed);
    build_center_column(fixed);
    build_right_column(fixed);
    build_bottom_buttons(fixed);

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

    if (g_fivrTimerId != 0) {
        g_source_remove(g_fivrTimerId);
        g_fivrTimerId = 0;
    }

    g_fivrAlive = TRUE;
    fivr_load_real_data();

    g_signal_connect(win, "destroy",
        G_CALLBACK(+[](GtkWidget*, gpointer) {

            if (g_fivrAlive && g_fivr.saveMode == 1) {
                fivr_save_config();
            }
            g_fivrAlive = FALSE;

            if (g_fivrTimerId != 0) {
                g_source_remove(g_fivrTimerId);
                g_fivrTimerId = 0;
            }
        }), NULL);
    g_fivrTimerId = g_timeout_add(1000, fivr_table_timer, NULL);

    g_object_set_data_full(G_OBJECT(win), "fivr-state",
                           new FivrState(g_fivr),
                           +[](gpointer p) { delete (FivrState*)p; });
    return win;
}

GtkWidget* test_fivr(void) { return dlg_fivr_control_new(NULL); }

extern "C" void fivr_load_real_data(void) {
    if (!g_fivrAlive) return;
    uv::Controller* c = fivr_ctl();
    if (!c) return;

    const gboolean savedUpdating = g_fivr.updating;
    g_fivr.updating = TRUE;

    char buf[64];

    fivr_refresh_offset_table();

    const auto bios = c->readBiosTurboRatios();
    if (!g_fivr.ratiosInited) {
        g_fivr.ratiosInited = true;

        auto readActive = [](uint32_t reg, bool convert) -> int {
            auto r = uv::MsrClient::readMsr(reg);
            if (!r) return 0;
            const int raw = static_cast<int>((*r >> 8) & 0xFF);
            if (raw <= 0 || raw >= 255) return 0;
            return convert ? uv::Controller::ratioToFreq(raw) : raw;
        };
        const int pActive = readActive(0x774, true);
        const int eActive = readActive(0x774, true);

        for (int i = 0; i < 8; ++i) {
            if (!g_fivr.edit_group_ratio[i]) continue;
            int val = (g_fivr.coreType == 1)
                      ? (eActive > 0 ? eActive : bios[i])
                      : (pActive > 0 ? pActive : bios[i]);
            if (val > 0) {
                snprintf(buf, sizeof(buf), "%d", val);
                gtk_editable_set_text(GTK_EDITABLE(g_fivr.edit_group_ratio[i]), buf);
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (!g_fivr.edit_allcore[i]) continue;
            int val = (g_fivr.coreType == 1)
                      ? (eActive > 0 ? eActive : bios[i])
                      : (pActive > 0 ? pActive : bios[i]);
            if (val > 0) {
                snprintf(buf, sizeof(buf), "%d", val);
                gtk_editable_set_text(GTK_EDITABLE(g_fivr.edit_allcore[i]), buf);
            }
        }
    }

    const auto pi = c->readPlatformInfo();
    if (g_fivr.edit_ntr1 && pi.maxNonTurboRatio > 0) {
        snprintf(buf, sizeof(buf), "%d", pi.maxNonTurboRatio);
        GTK_SAFE_SET_TEXT(g_fivr.edit_ntr1, buf);
    }
    if (g_fivr.edit_ntr2 && pi.maxNonTurboRatio > 0) {
        snprintf(buf, sizeof(buf), "%d", pi.maxNonTurboRatio);
        GTK_SAFE_SET_TEXT(g_fivr.edit_ntr2, buf);
    }

    const auto ring = c->readRingRatio();
    if (ring.ok) {
        if (g_fivr.edit_max) {
            snprintf(buf, sizeof(buf), "%d", ring.maxRing);
            GTK_SAFE_SET_TEXT(g_fivr.edit_max, buf);
        }
        if (g_fivr.edit_min) {
            snprintf(buf, sizeof(buf), "%d", ring.minRing);
            GTK_SAFE_SET_TEXT(g_fivr.edit_min, buf);
        }
    }

    const auto hwp = c->readHwpRequest();
    if (hwp.ok && g_fivr.edit_epp_val) {
        snprintf(buf, sizeof(buf), "%d", hwp.epp);
        GTK_SAFE_SET_TEXT(g_fivr.edit_epp_val, buf);
    }

    if (g_fivr.edit_voltage_id) {
        if (auto fv = c->readFreqVid()) {
            snprintf(buf, sizeof(buf), "%.4f", fv->vid);
            GTK_SAFE_SET_TEXT(g_fivr.edit_voltage_id, buf);
        }
    }

    if (g_fivr.edit_cache_ratio_val) {
        if (auto fv = c->readFreqVid()) {
            snprintf(buf, sizeof(buf), "%.0f", fv->multiplier);
            GTK_SAFE_SET_TEXT(g_fivr.edit_cache_ratio_val, buf);
        } else {
            GTK_SAFE_SET_TEXT(g_fivr.edit_cache_ratio_val, "-");
        }
    }

    if (g_fivr.edit_pl4) {
        GTK_SAFE_SET_TEXT(g_fivr.edit_pl4, "-");
    }

    if (g_fivr.edit_iccmax) {
        if (auto icc = c->readIccMax(static_cast<uv::Plane>(g_fivr.domain))) {
            snprintf(buf, sizeof(buf), "%.2f", *icc);
            GTK_SAFE_SET_TEXT(g_fivr.edit_iccmax, buf);
        } else {
            GTK_SAFE_SET_TEXT(g_fivr.edit_iccmax, "N/A");
        }
    }
    if (g_fivr.edit_cache_ratio) {
        if (auto fv = c->readFreqVid()) {
            snprintf(buf, sizeof(buf), "%.0f", fv->multiplier);
            GTK_SAFE_SET_TEXT(g_fivr.edit_cache_ratio, buf);
        } else {
            GTK_SAFE_SET_TEXT(g_fivr.edit_cache_ratio, "-");
        }
    }

    const auto misc = c->readMiscFlags();
    if (misc.ok) {

        if (g_fivr.chk_avx)        gtk_widget_set_sensitive(g_fivr.chk_avx, FALSE);
        if (g_fivr.edit_avx)       gtk_widget_set_sensitive(g_fivr.edit_avx, FALSE);
        if (g_fivr.chk_mvboost)    gtk_widget_set_sensitive(g_fivr.chk_mvboost, FALSE);
        if (g_fivr.edit_mvboost)   gtk_widget_set_sensitive(g_fivr.edit_mvboost, FALSE);
        if (g_fivr.chk_overclock)  gtk_widget_set_sensitive(g_fivr.chk_overclock, FALSE);
        if (g_fivr.chk_vmax)       gtk_widget_set_sensitive(g_fivr.chk_vmax, FALSE);
        if (g_fivr.chk_ring_down)  gtk_widget_set_sensitive(g_fivr.chk_ring_down, FALSE);
        if (g_fivr.chk_tvb)        gtk_widget_set_sensitive(g_fivr.chk_tvb, c->tvbWritable());

        if (g_fivr.edit_avx) {
            snprintf(buf, sizeof(buf), "%d", misc.avx2Offset);
            GTK_SAFE_SET_TEXT(g_fivr.edit_avx, buf);
        }

        if (g_fivr.chk_tvb) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_tvb), misc.tvbCaps);
            gtk_widget_set_sensitive(g_fivr.chk_tvb, c->tvbWritable());
        }

        if (g_fivr.chk_ring_down) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_ring_down),
                                        misc.ringDown);
            gtk_widget_set_sensitive(g_fivr.chk_ring_down, FALSE);
        }

        if (g_fivr.chk_vmax) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_vmax),
                                        misc.vmaxStress);
            gtk_widget_set_sensitive(g_fivr.chk_vmax, FALSE);
        }

        if (g_fivr.chk_mvboost) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_mvboost), FALSE);
        }
        if (g_fivr.edit_mvboost) {
            GTK_SAFE_SET_TEXT(g_fivr.edit_mvboost, "-");
        }

        if (g_fivr.chk_overclock) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_overclock), FALSE);
        }
    }

    if (g_fivr.lbl_ucode) {
        if (auto rev = c->readMicrocodeRevision()) {
            snprintf(buf, sizeof(buf), "uCode %u", *rev);
        } else {
            snprintf(buf, sizeof(buf), "uCode -");
        }
        gtk_label_set_text(GTK_LABEL(g_fivr.lbl_ucode), buf);
    }

    if (g_fivr.edit_extra_voltage) {
        const uint64_t v1ad = 0;
        (void)v1ad;

        GTK_SAFE_SET_TEXT(g_fivr.edit_extra_voltage, "0");
    }

    if (g_fivr.btn_allcore[0]) {
        const auto bios = c->readBiosTurboRatios();

        int uniq[4] = {0, 0, 0, 0};
        int n = 0;
        for (int i = 0; i < 8 && n < 4; ++i) {
            bool dup = false;
            for (int j = 0; j < n; ++j) if (uniq[j] == bios[i]) dup = true;
            if (!dup && bios[i] > 0) uniq[n++] = bios[i];
        }
        for (int i = 0; i < 4; ++i) {
            char b2[8];
            if (i < n) snprintf(b2, sizeof(b2), "%d", uniq[i]);
            else snprintf(b2, sizeof(b2), "-");
            gtk_button_set_label(GTK_BUTTON(g_fivr.btn_allcore[i]), b2);
        }
    }

    {
        const auto pi2 = c->readPlatformInfo();
        if (g_fivr.edit_overclock && GTK_IS_EDITABLE(g_fivr.edit_overclock)) {
            g_fivr.updating = TRUE;
            gtk_editable_set_text(GTK_EDITABLE(g_fivr.edit_overclock),
                pi2.programmableRatio ? "unlimited" : "locked");
            g_fivr.updating = FALSE;
        }
    }

    if (g_fivr.edit_ntr_lock) {
        const auto pi3 = c->readPlatformInfo();
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.edit_ntr_lock),
                                    pi3.ratioLocked);
    }

    {
        const auto st = uv::loadSettings();
        if (st.valid) {
            g_fivr.updating = TRUE;

            if (st.fivr.domain >= 0 && st.fivr.domain < 6 && g_fivr.radio_domain[st.fivr.domain]) {
                gtk_check_button_set_active(
                    GTK_CHECK_BUTTON(g_fivr.radio_domain[st.fivr.domain]), TRUE);
                g_fivr.domain = st.fivr.domain;
            }

            if (st.fivr.adaptive && g_fivr.radio_adaptive)
                gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.radio_adaptive), TRUE);
            else if (g_fivr.radio_static)
                gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.radio_static), TRUE);

            if (st.fivr.saveMode >= 0 && st.fivr.saveMode < 3 && g_fivr.save_opts[st.fivr.saveMode])
                gtk_check_button_set_active(
                    GTK_CHECK_BUTTON(g_fivr.save_opts[st.fivr.saveMode]), TRUE);
            g_fivr.saveMode = st.fivr.saveMode;

            if (g_fivr.edit_avx) {
                snprintf(buf, sizeof(buf), "%d", st.fivr.avxOffset);
                GTK_SAFE_SET_TEXT(g_fivr.edit_avx, buf);
            }
            if (g_fivr.chk_tvb)
                gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_tvb), st.fivr.tvb);
            if (g_fivr.chk_ring_down)
                gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_ring_down), st.fivr.ringDownBin);
            if (g_fivr.chk_vmax)
                gtk_check_button_set_active(GTK_CHECK_BUTTON(g_fivr.chk_vmax), st.fivr.vMaxStress);
            g_fivr.updating = FALSE;
        }
    }

    fivr_sync_domain();
    g_fivr.updating = savedUpdating;
}

static void fivr_switch_core_group(int type) {
    uv::Controller* c = fivr_ctl();
    if (!c) return;

    static int pBuf[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
    static int eBuf[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
    static int lastType = 0;

    int* curBuf = (lastType == 0) ? pBuf : eBuf;
    for (int i = 0; i < 8; ++i) {
        if (!g_fivr.edit_group_ratio[i]) continue;
        const char* t = gtk_editable_get_text(
            GTK_EDITABLE(g_fivr.edit_group_ratio[i]));
        if (t && *t) curBuf[i] = atoi(t);
    }

    int* dstBuf = (type == 0) ? pBuf : eBuf;
    const auto bios = c->readBiosTurboRatios();
    const auto topo = c->readCoreTopology();
    const int eRatio = topo.eMaxKHz > 0 ? topo.eMaxKHz / 100000 : 0;

    int activeRatio = 0;
    {
        const uint32_t reg = (type == 1) ? 0x772 : 0x774;
        uint64_t v = 0;
        if (auto r = uv::MsrClient::readMsr(reg)) v = *r;
        activeRatio = static_cast<int>((v >> 8) & 0xFF);
    }

    g_fivr.updating = TRUE;
    for (int i = 0; i < 8; ++i) {
        if (!g_fivr.edit_group_ratio[i]) continue;
        int val;
        if (dstBuf[i] > 0) {

            val = dstBuf[i];
        } else if (activeRatio > 0 && activeRatio < 255) {

            val = activeRatio;
        } else {

            val = bios[i];
            if (type == 1 && eRatio > 0 && val > eRatio) val = eRatio;
        }
        if (val > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", val);
            gtk_editable_set_text(GTK_EDITABLE(g_fivr.edit_group_ratio[i]), buf);
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (!g_fivr.edit_allcore[i]) continue;
        int val = dstBuf[i] > 0 ? dstBuf[i] : bios[i];
        if (type == 1 && eRatio > 0 && val > eRatio) val = eRatio;
        if (val > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", val);
            gtk_editable_set_text(GTK_EDITABLE(g_fivr.edit_allcore[i]), buf);
        }
    }
    g_fivr.updating = FALSE;

    g_fivr.coreType = type;
    lastType = type;
}

static void fivr_apply_all(void) {
    uv::Controller* c = fivr_ctl();
    if (!c) return;

    auto readSpin = [](GtkWidget* w) -> int {
        if (!w || !GTK_IS_EDITABLE(w)) return 0;
        const char* t = gtk_editable_get_text(GTK_EDITABLE(w));
        if (!t || !*t) return 0;
        const int v = atoi(t);
        return (v > 0 && v < 256) ? v : 0;
    };

    int ratioMax = 0;
    for (int i = 0; i < 8; ++i) {
        const int v = readSpin(g_fivr.edit_group_ratio[i]);
        if (v > ratioMax) ratioMax = v;
    }
    g_printerr("[turbo] ratioMax=%d coreType=%d target=MSR 0x%03X\n",
               ratioMax, g_fivr.coreType,
               g_fivr.coreType == 1 ? 0x772 : 0x774);
    if (ratioMax > 0) {

        try {
            if (g_fivr.coreType == 1) {
                c->setTurboRatioE(ratioMax);
            } else {
                c->setTurboRatioP(ratioMax);

                g_printerr("[turbo] 已写 0x774 bit[15:8]=%d；"
                           "注意：本平台 HWP 硬件自治，倍频设置仅供参考\n",
                           ratioMax);
            }
            g_printerr("[turbo] 写入成功\n");
        } catch (const std::exception& e) {
            g_printerr("[turbo] 写入失败: %s\n", e.what());

            if (g_fivr.coreType == 1 && g_fivr.edit_group_ratio[0]) {
                gtk_editable_set_text(
                    GTK_EDITABLE(g_fivr.edit_group_ratio[0]), "N/A");
            }
        }
    }

    if (g_fivr.edit_offset) {
        const char* txt = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_offset));
        if (txt && *txt) {
            const double mv = atof(txt);
            if (mv >= -1000.0 && mv <= 1000.0) {
                const int plane = domain_to_plane(g_fivr.domain);
                try {
                    if (plane >= 0) {
                        c->setOffset(static_cast<uv::Plane>(plane), mv, false);
                    } else {
                        c->writeOffsetByDomain(g_fivr.domain, mv);
                    }
                } catch (...) {}
            }
        }
    }

    if (g_fivr.edit_cache_ratio) {
        const char* txt = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_cache_ratio));
        if (txt && *txt) {
            const int r = atoi(txt);
            if (r > 0 && r <= 255) {
                try { c->setCacheRatio(r); } catch (...) {}
            }
        }
    }

    if (g_fivr.edit_avx) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_avx));
        if (t && *t && strcmp(t, "-") != 0) {
            const int v = atoi(t);
            if (v >= 0 && v <= 255) {
                try { c->writeAvxOffset(v); } catch (...) {}
            }
        }
    }

    if (g_fivr.chk_tvb && gtk_widget_get_sensitive(g_fivr.chk_tvb)) {
        try {
            c->writeTvb(gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.chk_tvb)));
        } catch (...) {}
    }

    if (g_fivr.chk_ring_down) {
        try {
            c->writeRingDownBin(
                gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.chk_ring_down)));
        } catch (...) {}
    }

    if (g_fivr.edit_iccmax) {
        const char* txt = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_iccmax));
        if (txt && *txt && strcmp(txt, "N/A") != 0) {
            const double a = atof(txt);
            if (a > 0 && a <= 1000) {
                try {
                    c->setIccMax(static_cast<uv::Plane>(g_fivr.domain), a);
                } catch (...) {}
            }
        }
    }

    if (g_fivr.saveMode == 2) {
        fivr_save_config();
    }

    {
        auto st = uv::loadSettings();
        st.fivr.domain = g_fivr.domain;
        st.fivr.adaptive = g_fivr.radio_adaptive
            ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.radio_adaptive)) : true;
        st.fivr.saveMode = g_fivr.saveMode;

        bool ok2 = false;
        const auto planes = c->readAllOffsets(&ok2);
        if (ok2) {

            for (int i = 0; i < uv::kPlaneCount && i < 6; ++i) {
                st.fivr.offsets[i] = planes[i];
            }
        }

        if (g_fivr.edit_avx) {
            const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_avx));
            if (t && *t && strcmp(t, "-") != 0) st.fivr.avxOffset = atoi(t);
        }
        if (g_fivr.chk_tvb)
            st.fivr.tvb = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.chk_tvb));
        if (g_fivr.chk_ring_down)
            st.fivr.ringDownBin = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.chk_ring_down));
        if (g_fivr.chk_vmax)
            st.fivr.vMaxStress = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.chk_vmax));
        uv::saveSettings(st);
    }

    fivr_refresh_offset_table();
    fivr_load_real_data();
}

static void fivr_save_config(void) {

    auto st = uv::loadSettings();

    st.fivr.domain = g_fivr.domain;
    st.fivr.adaptive = g_fivr.radio_adaptive
        ? gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.radio_adaptive))
        : true;
    st.fivr.saveMode = g_fivr.saveMode;

    for (int i = 0; i < 6; ++i) {
        if (g_domainHasBuffer[i]) {
            st.fivr.offsets[i] = g_domainBuffer[i];
        } else if (i < uv::kPlaneCount) {
            auto mv = uv::MsrClient::readOffset(i);
            if (mv) st.fivr.offsets[i] = *mv;
        }
    }

    if (g_fivr.combo_range) {
        st.fivr.rangeMv = (int)gtk_drop_down_get_selected(
            GTK_DROP_DOWN(g_fivr.combo_range));
    }

    if (g_fivr.edit_iccmax && GTK_IS_EDITABLE(g_fivr.edit_iccmax)) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_iccmax));
        if (t && *t && strcmp(t, "N/A") != 0) st.fivr.iccMax = atoi(t);
    }
    if (g_fivr.edit_cache_ratio && GTK_IS_EDITABLE(g_fivr.edit_cache_ratio)) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_cache_ratio));
        if (t && *t && strcmp(t, "-") != 0) st.fivr.cacheRatio = atoi(t);
    }

    if (g_fivr.edit_avx && GTK_IS_EDITABLE(g_fivr.edit_avx)) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_avx));
        if (t && *t && strcmp(t, "-") != 0) st.fivr.avxOffset = atoi(t);
    }
    if (g_fivr.chk_tvb)
        st.fivr.tvb = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_fivr.chk_tvb));
    if (g_fivr.chk_ring_down)
        st.fivr.ringDownBin = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(g_fivr.chk_ring_down));
    if (g_fivr.chk_vmax)
        st.fivr.vMaxStress = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(g_fivr.chk_vmax));
    if (g_fivr.chk_mvboost)
        st.fivr.mvBoost = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(g_fivr.chk_mvboost));
    if (g_fivr.edit_mvboost && GTK_IS_EDITABLE(g_fivr.edit_mvboost)) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_mvboost));
        if (t && *t && strcmp(t, "-") != 0) st.fivr.mvBoostMv = atoi(t);
    }
    if (g_fivr.chk_overclock)
        st.fivr.overclock = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(g_fivr.chk_overclock));

    if (g_fivr.chk_unlock)
        st.fivr.unlocked = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(g_fivr.chk_unlock));

    for (int i = 0; i < 8; ++i) {
        if (g_fivr.edit_group_ratio[i] && GTK_IS_EDITABLE(g_fivr.edit_group_ratio[i])) {
            const char* t = gtk_editable_get_text(
                GTK_EDITABLE(g_fivr.edit_group_ratio[i]));
            if (t && *t) st.turbo.ratios[i] = atoi(t);
        }
    }

    if (g_fivr.edit_overclock && GTK_IS_EDITABLE(g_fivr.edit_overclock)) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_overclock));
        if (t && *t) st.turbo.extraVoltage = atoi(t);
    }
    if (g_fivr.chk_lock)
        st.turbo.ntrLock = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(g_fivr.chk_lock));
    if (g_fivr.edit_ntr1 && GTK_IS_EDITABLE(g_fivr.edit_ntr1)) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_ntr1));
        if (t && *t) st.turbo.ntrRatio1 = atoi(t);
    }
    if (g_fivr.edit_ntr2 && GTK_IS_EDITABLE(g_fivr.edit_ntr2)) {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_fivr.edit_ntr2));
        if (t && *t) st.turbo.ntrRatio2 = atoi(t);
    }
    if (g_fivr.chk_unlimited)
        st.turbo.unlimited = gtk_check_button_get_active(
            GTK_CHECK_BUTTON(g_fivr.chk_unlimited));

    st.mainwin.activeProfile = g_fivr.activeProfile;

    uv::saveSettings(st);
}
