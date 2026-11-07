

#include "dialogs.h"
#include "i18n.h"

#include "../../include/undervolt.hpp"
#include "../../include/msr_daemon.hpp"
#include "../../include/settings.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

extern double ts_scale(void);
#define S(v) ((int)((v) * ts_scale()))

namespace {
uv::Controller* cs_ctl() {
    static uv::Controller* c = nullptr;
    if (!c) { try { uv::MsrClient::ensureDaemon(); c = new uv::Controller(); } catch (...) {} }
    return c;
}
}

typedef struct {
    GtkWidget* win;
    GtkWidget* chk_enable;
    GtkWidget* chk_c1_demote;
    GtkWidget* chk_c3_demote;
    GtkWidget* chk_c1_undemote;
    GtkWidget* chk_c3_undemote;
    GtkWidget* spin_pkg_limit;
    GtkWidget* profile[4];
    GtkWidget* lbl_lock;
    GtkWidget* lbl_status;
} CsState;

static CsState g_cs;

static void cs_put(GtkWidget* fixed, GtkWidget* w, int x, int y, int wd, int ht) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    gtk_widget_set_size_request(w, S(wd), S(ht));
}

static GtkWidget* cs_group(const char* t) {
    GtkWidget* f = gtk_frame_new(NULL);
    gtk_widget_add_css_class(f, "ts-red-group");
    GtkWidget* l = gtk_label_new(t);
    gtk_widget_add_css_class(l, "ts-red-title");
    gtk_frame_set_label_widget(GTK_FRAME(f), l);
    return f;
}

static void cs_load(void) {
    uv::Controller* c = cs_ctl();
    if (!c) return;

    const auto cfg = c->readCStateConfig();
    if (!cfg.ok) {
        if (g_cs.lbl_status)
            gtk_label_set_text(GTK_LABEL(g_cs.lbl_status),
                               "MSR 0xE2 读取失败");
        return;
    }

    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_cs.chk_enable),
                                cfg.cStateEnable);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_cs.chk_c1_demote),
                                cfg.c1Demotion);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_cs.chk_c3_demote),
                                cfg.c3Demotion);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_cs.chk_c1_undemote),
                                cfg.c1Undemotion);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_cs.chk_c3_undemote),
                                cfg.c3Undemotion);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_cs.spin_pkg_limit),
                              cfg.pkgCStateLimit);

    char buf[96];
    if (cfg.locked) {
        snprintf(buf, sizeof(buf), "已锁定（bit31=1），不可修改");
        gtk_widget_add_css_class(g_cs.lbl_lock, "ts-prochot");
        GtkWidget* items[] = {
            g_cs.chk_enable, g_cs.chk_c1_demote, g_cs.chk_c3_demote,
            g_cs.chk_c1_undemote, g_cs.chk_c3_undemote, g_cs.spin_pkg_limit,
        };
        for (GtkWidget* w : items) if (w) gtk_widget_set_sensitive(w, FALSE);
    } else {
        snprintf(buf, sizeof(buf), "未锁定，可修改");
        gtk_widget_remove_css_class(g_cs.lbl_lock, "ts-prochot");
    }
    gtk_label_set_text(GTK_LABEL(g_cs.lbl_lock), buf);

    if (g_cs.lbl_status) {
        snprintf(buf, sizeof(buf),
                 "MSR 0xE2: enable=%d C1d=%d C3d=%d C1u=%d C3u=%d pkgLimit=%d",
                 (int)cfg.cStateEnable, (int)cfg.c1Demotion,
                 (int)cfg.c3Demotion, (int)cfg.c1Undemotion,
                 (int)cfg.c3Undemotion, cfg.pkgCStateLimit);
        gtk_label_set_text(GTK_LABEL(g_cs.lbl_status), buf);
    }
}

static void cs_apply(void) {
    uv::Controller* c = cs_ctl();
    if (!c) return;

    auto cfg = c->readCStateConfig();
    if (!cfg.ok) return;

    if (cfg.locked) {
        if (g_cs.lbl_status)
            gtk_label_set_text(GTK_LABEL(g_cs.lbl_status),
                               "写入失败：MSR 0xE2 已被锁定（bit31=1）");
        return;
    }

    cfg.cStateEnable  = gtk_check_button_get_active(
                            GTK_CHECK_BUTTON(g_cs.chk_enable));
    cfg.c1Demotion    = gtk_check_button_get_active(
                            GTK_CHECK_BUTTON(g_cs.chk_c1_demote));
    cfg.c3Demotion    = gtk_check_button_get_active(
                            GTK_CHECK_BUTTON(g_cs.chk_c3_demote));
    cfg.c1Undemotion  = gtk_check_button_get_active(
                            GTK_CHECK_BUTTON(g_cs.chk_c1_undemote));
    cfg.c3Undemotion  = gtk_check_button_get_active(
                            GTK_CHECK_BUTTON(g_cs.chk_c3_undemote));
    cfg.pkgCStateLimit = gtk_spin_button_get_value_as_int(
                            GTK_SPIN_BUTTON(g_cs.spin_pkg_limit));

    const bool ok = c->writeCStateConfig(cfg);
    if (g_cs.lbl_status) {
        gtk_label_set_text(GTK_LABEL(g_cs.lbl_status),
                           ok ? "已写入 MSR 0xE2" : "写入被硬件拒绝");
    }

    struct timespec ts = {0, 50 * 1000 * 1000};
    nanosleep(&ts, 0);
    cs_load();

    {
        auto st = uv::loadSettings();
        st.cstates.cStateEnable = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_cs.chk_enable));
        st.cstates.c1Demotion   = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_cs.chk_c1_demote));
        st.cstates.c3Demotion   = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_cs.chk_c3_demote));
        st.cstates.c1Undemotion = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_cs.chk_c1_undemote));
        st.cstates.c3Undemotion = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_cs.chk_c3_undemote));
        uv::saveSettings(st);
    }
}

GtkWidget* dlg_cstates_new(GtkWindow* parent) {
    memset(&g_cs, 0, sizeof(g_cs));

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "C States");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), S(390), S(340));
    g_cs.win = win;

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(390), S(340));

    GtkWidget* gp = cs_group("Profile");
    cs_put(fixed, gp, 10, 6, 360, 48);
    for (int i = 0; i < 4; ++i) {
        GtkWidget* rb = gtk_check_button_new();
        if (i > 0) {
            gtk_check_button_set_group(GTK_CHECK_BUTTON(rb),
                                       GTK_CHECK_BUTTON(g_cs.profile[0]));
        }
        g_cs.profile[i] = rb;
        cs_put(fixed, rb, 40 + i * 80, 26, 16, 16);
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_cs.profile[0]), TRUE);

    GtkWidget* gpkg = cs_group("Package C State");
    cs_put(fixed, gpkg, 10, 62, 360, 56);

    cs_put(fixed, gtk_label_new("C State Enable"), 22, 84, 96, 16);
    g_cs.chk_enable = gtk_check_button_new_with_label("Enable");
    cs_put(fixed, g_cs.chk_enable, 122, 82, 72, 20);

    {
        GtkWidget* pl = gtk_label_new("Pkg Limit");
        gtk_widget_set_halign(pl, GTK_ALIGN_END);
        gtk_label_set_xalign(GTK_LABEL(pl), 1.0f);
        cs_put(fixed, pl, 200, 84, 58, 16);
    }
    g_cs.spin_pkg_limit = gtk_spin_button_new_with_range(0, 15, 1);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(g_cs.spin_pkg_limit), TRUE);
    gtk_widget_add_css_class(g_cs.spin_pkg_limit, "ts-spin");
    cs_put(fixed, g_cs.spin_pkg_limit, 262, 82, 72, 24);

    GtkWidget* gd = cs_group("Demotion Undemotion");
    cs_put(fixed, gd, 10, 126, 360, 100);

    cs_put(fixed, gtk_label_new("C1"), 26, 150, 30, 16);
    cs_put(fixed, gtk_label_new("C3"), 26, 176, 30, 16);

    g_cs.chk_c1_demote = gtk_check_button_new_with_label("Demote");
    cs_put(fixed, g_cs.chk_c1_demote, 60, 148, 80, 20);
    g_cs.chk_c3_demote = gtk_check_button_new_with_label("Demote");
    cs_put(fixed, g_cs.chk_c3_demote, 60, 174, 80, 20);

    g_cs.chk_c1_undemote = gtk_check_button_new_with_label("Undemote");
    cs_put(fixed, g_cs.chk_c1_undemote, 150, 148, 95, 20);
    g_cs.chk_c3_undemote = gtk_check_button_new_with_label("Undemote");
    cs_put(fixed, g_cs.chk_c3_undemote, 150, 174, 95, 20);

    cs_put(fixed, gtk_label_new("（低功耗态降级/恢复控制）"), 255, 160, 110, 16);

    g_cs.lbl_lock = gtk_label_new("");
    gtk_widget_set_halign(g_cs.lbl_lock, GTK_ALIGN_START);
    cs_put(fixed, g_cs.lbl_lock, 10, 234, 360, 18);

    g_cs.lbl_status = gtk_label_new("");
    gtk_widget_add_css_class(g_cs.lbl_status, "ts-topo");
    gtk_widget_set_halign(g_cs.lbl_status, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_cs.lbl_status), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(g_cs.lbl_status), 56);
    cs_put(fixed, g_cs.lbl_status, 10, 256, 360, 34);

    GtkWidget* ok = gtk_button_new_with_label("OK");
    g_object_set_data(G_OBJECT(ok), "dlgwin", win);
    gtk_widget_add_css_class(ok, "ts-btn");
    cs_put(fixed, ok, 130, 298, 80, 24);
    g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        cs_apply();
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
    }), NULL);

    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    g_object_set_data(G_OBJECT(cancel), "dlgwin", win);
    gtk_widget_add_css_class(cancel, "ts-btn");
    cs_put(fixed, cancel, 218, 298, 80, 24);
    g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
    }), NULL);

    GtkWidget* apply = gtk_button_new_with_label("Apply");
    g_object_set_data(G_OBJECT(apply), "dlgwin", win);
    gtk_widget_add_css_class(apply, "ts-btn");
    cs_put(fixed, apply, 306, 298, 80, 24);
    g_signal_connect(apply, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        cs_apply();
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
    cs_load();
    return win;
}
