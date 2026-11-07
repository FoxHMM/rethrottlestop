

#include "dialogs.h"
#include "i18n.h"

#include "../../include/undervolt.hpp"
#include "../../include/msr_daemon.hpp"
#include "../../include/settings.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern double ts_scale(void);
#define S(v) ((int)((v) * ts_scale()))

namespace {
uv::Controller* tpl_ctl() {
    static uv::Controller* c = nullptr;
    if (!c) { try { uv::MsrClient::ensureDaemon(); c = new uv::Controller(); } catch (...) {} }
    return c;
}
}

typedef struct {
    GtkWidget* win;
    GtkWidget* edit_pl1;
    GtkWidget* scale_pl1;
    GtkWidget* edit_pl2;
    GtkWidget* scale_pl2;
    GtkWidget* edit_pl1_win;
    GtkWidget* edit_pl2_win;
    GtkWidget* edit_pl1_max;
    GtkWidget* lbl_status;
    gboolean updating;
} TplState;

static TplState g_tpl;

static void tpl_put(GtkWidget* fixed, GtkWidget* w, int x, int y, int wd, int ht) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    gtk_widget_set_size_request(w, S(wd), S(ht));
}

static GtkWidget* tpl_red_group(const char* title) {
    GtkWidget* f = gtk_frame_new(NULL);
    gtk_widget_add_css_class(f, "ts-red-group");
    GtkWidget* lbl = gtk_label_new(title);
    gtk_widget_add_css_class(lbl, "ts-red-title");
    gtk_frame_set_label_widget(GTK_FRAME(f), lbl);
    return f;
}

static GtkWidget* tpl_edit(const char* text) {
    GtkWidget* e = gtk_entry_new();
    if (text) gtk_editable_set_text(GTK_EDITABLE(e), text);
    gtk_widget_add_css_class(e, "ts-edit");
    return e;
}

static void tpl_load(void) {
    uv::Controller* c = tpl_ctl();
    if (!c) return;

    const auto pl = c->readPowerLimits();
    if (!pl.ok) {
        gtk_label_set_text(GTK_LABEL(g_tpl.lbl_status),
                           "RAPL 不可用：无法读取功率限制");
        return;
    }

    char buf[64];
    g_tpl.updating = TRUE;

    snprintf(buf, sizeof(buf), "%.1f", pl.pl1W);
    gtk_editable_set_text(GTK_EDITABLE(g_tpl.edit_pl1), buf);
    gtk_range_set_value(GTK_RANGE(g_tpl.scale_pl1), pl.pl1W);

    snprintf(buf, sizeof(buf), "%.1f", pl.pl2W);
    gtk_editable_set_text(GTK_EDITABLE(g_tpl.edit_pl2), buf);
    gtk_range_set_value(GTK_RANGE(g_tpl.scale_pl2), pl.pl2W);

    snprintf(buf, sizeof(buf), "%.0f", pl.pl1WindowUs);
    gtk_editable_set_text(GTK_EDITABLE(g_tpl.edit_pl1_win), buf);

    snprintf(buf, sizeof(buf), "%.0f", pl.pl2WindowUs);
    gtk_editable_set_text(GTK_EDITABLE(g_tpl.edit_pl2_win), buf);

    snprintf(buf, sizeof(buf), "%.1f", pl.pl1MaxW);
    gtk_editable_set_text(GTK_EDITABLE(g_tpl.edit_pl1_max), buf);

    g_tpl.updating = FALSE;

    snprintf(buf, sizeof(buf), "当前：PL1 %.1f W / PL2 %.1f W（来自 RAPL）",
             pl.pl1W, pl.pl2W);
    gtk_label_set_text(GTK_LABEL(g_tpl.lbl_status), buf);
}

static void tpl_save_settings(double p1, double p2, double t1, double t2);

static void tpl_load_settings(void) {
    const auto st = uv::loadSettings();
    if (!st.valid) return;
    char buf[32];
    if (g_tpl.edit_pl1 && st.power.pl1W > 0) {
        snprintf(buf, sizeof(buf), "%.1f", st.power.pl1W);
        gtk_editable_set_text(GTK_EDITABLE(g_tpl.edit_pl1), buf);
    }
    if (g_tpl.edit_pl2 && st.power.pl2W > 0) {
        snprintf(buf, sizeof(buf), "%.1f", st.power.pl2W);
        gtk_editable_set_text(GTK_EDITABLE(g_tpl.edit_pl2), buf);
    }
}

static void tpl_apply(void) {
    uv::Controller* c = tpl_ctl();
    if (!c) return;

    const double pl1 = atof(gtk_editable_get_text(GTK_EDITABLE(g_tpl.edit_pl1)));
    const double pl2 = atof(gtk_editable_get_text(GTK_EDITABLE(g_tpl.edit_pl2)));

    char msg[160];
    bool ok1 = false, ok2 = false;
    if (pl1 > 0) ok1 = c->setPl1(pl1);
    if (pl2 > 0) ok2 = c->setPl2(pl2);

    if (ok1 && ok2) {
        snprintf(msg, sizeof(msg), "已写入：PL1 %.1f W / PL2 %.1f W", pl1, pl2);
    } else if (ok1) {
        snprintf(msg, sizeof(msg), "PL1 写入成功，PL2 失败（可能被 BIOS 锁定）");
    } else if (ok2) {
        snprintf(msg, sizeof(msg), "PL2 写入成功，PL1 失败（可能被 BIOS 锁定）");
    } else {
        snprintf(msg, sizeof(msg), "写入失败：需要 root 权限或 BIOS 锁定");
    }
    gtk_label_set_text(GTK_LABEL(g_tpl.lbl_status), msg);

    tpl_load();

    {
        auto st = uv::loadSettings();
        if (g_tpl.edit_pl1) {
            const char* v = gtk_editable_get_text(GTK_EDITABLE(g_tpl.edit_pl1));
            if (v && *v) st.power.pl1W = atof(v);
        }
        if (g_tpl.edit_pl2) {
            const char* v = gtk_editable_get_text(GTK_EDITABLE(g_tpl.edit_pl2));
            if (v && *v) st.power.pl2W = atof(v);
        }
        if (g_tpl.edit_pl1_win) {
            const char* v = gtk_editable_get_text(GTK_EDITABLE(g_tpl.edit_pl1_win));
            if (v && *v) st.power.pl1WindowUs = atof(v);
        }
        if (g_tpl.edit_pl2_win) {
            const char* v = gtk_editable_get_text(GTK_EDITABLE(g_tpl.edit_pl2_win));
            if (v && *v) st.power.pl2WindowUs = atof(v);
        }
        uv::saveSettings(st);
    }
}

GtkWidget* dlg_turbo_power_new(GtkWindow* parent) {
    memset(&g_tpl, 0, sizeof(g_tpl));

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Turbo Power Limits");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), S(560), S(400));
    g_tpl.win = win;

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(560), S(400));

    GtkWidget* g1 = tpl_red_group("Long Power Limit (PL1)");
    tpl_put(fixed, g1, 14, 20, 250, 110);
    tpl_put(fixed, gtk_label_new("Power Limit"), 26, 44, 90, 16);
    g_tpl.edit_pl1 = tpl_edit("");
    tpl_put(fixed, g_tpl.edit_pl1, 130, 42, 70, 20);
    tpl_put(fixed, gtk_label_new("W"), 206, 44, 24, 16);
    g_tpl.scale_pl1 = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 250, 1);
    gtk_scale_set_draw_value(GTK_SCALE(g_tpl.scale_pl1), FALSE);
    tpl_put(fixed, g_tpl.scale_pl1, 26, 68, 220, 18);
    tpl_put(fixed, gtk_label_new("Time Window"), 26, 92, 90, 16);
    g_tpl.edit_pl1_win = tpl_edit("");
    tpl_put(fixed, g_tpl.edit_pl1_win, 130, 90, 70, 20);
    tpl_put(fixed, gtk_label_new("us"), 206, 92, 24, 16);

    GtkWidget* g2 = tpl_red_group("Short Power Limit (PL2)");
    tpl_put(fixed, g2, 14, 142, 250, 110);
    tpl_put(fixed, gtk_label_new("Power Limit"), 26, 166, 90, 16);
    g_tpl.edit_pl2 = tpl_edit("");
    tpl_put(fixed, g_tpl.edit_pl2, 130, 164, 70, 20);
    tpl_put(fixed, gtk_label_new("W"), 206, 166, 24, 16);
    g_tpl.scale_pl2 = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 250, 1);
    gtk_scale_set_draw_value(GTK_SCALE(g_tpl.scale_pl2), FALSE);
    tpl_put(fixed, g_tpl.scale_pl2, 26, 190, 220, 18);
    tpl_put(fixed, gtk_label_new("Time Window"), 26, 214, 90, 16);
    g_tpl.edit_pl2_win = tpl_edit("");
    tpl_put(fixed, g_tpl.edit_pl2_win, 130, 212, 70, 20);
    tpl_put(fixed, gtk_label_new("us"), 206, 214, 24, 16);

    GtkWidget* g3 = tpl_red_group("Hardware Info");
    tpl_put(fixed, g3, 290, 20, 250, 120);
    tpl_put(fixed, gtk_label_new("PL1 Max"), 302, 44, 80, 16);
    g_tpl.edit_pl1_max = tpl_edit("");
    gtk_editable_set_editable(GTK_EDITABLE(g_tpl.edit_pl1_max), FALSE);
    gtk_widget_add_css_class(g_tpl.edit_pl1_max, "ts-locked");
    tpl_put(fixed, g_tpl.edit_pl1_max, 390, 42, 70, 20);
    tpl_put(fixed, gtk_label_new("W"), 466, 44, 24, 16);
    {
        GtkWidget* src = gtk_label_new("数据源：RAPL powercap\n"
                                       "/sys/class/powercap/\nintel-rapl:0/");
        tpl_put(fixed, src, 302, 74, 226, 56);
    }

    g_tpl.lbl_status = gtk_label_new("");
    gtk_widget_add_css_class(g_tpl.lbl_status, "ts-topo");
    gtk_widget_set_halign(g_tpl.lbl_status, GTK_ALIGN_START);
    tpl_put(fixed, g_tpl.lbl_status, 14, 266, 526, 24);

    GtkWidget* ok = gtk_button_new_with_label("OK");
    g_object_set_data(G_OBJECT(ok), "dlgwin", win);
    gtk_widget_add_css_class(ok, "ts-btn");
    tpl_put(fixed, ok, 180, 310, 100, 26);
    g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        tpl_apply();
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
    }), NULL);

    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    g_object_set_data(G_OBJECT(cancel), "dlgwin", win);
    gtk_widget_add_css_class(cancel, "ts-btn");
    tpl_put(fixed, cancel, 290, 310, 100, 26);
    g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
    }), NULL);

    GtkWidget* apply = gtk_button_new_with_label("Apply");
    g_object_set_data(G_OBJECT(apply), "dlgwin", win);
    gtk_widget_add_css_class(apply, "ts-btn");
    tpl_put(fixed, apply, 400, 310, 100, 26);
    g_signal_connect(apply, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        tpl_apply();
    }), NULL);

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
    tpl_load();

    return win;
}
