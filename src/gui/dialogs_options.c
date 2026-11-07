

#include "dialogs.h"
#include "i18n.h"

#include "../../include/undervolt.hpp"
#include "../../include/msr_daemon.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern double ts_scale(void);
#define S(v) ((int)((v) * ts_scale()))

namespace {
uv::Controller* op_ctl() {
    static uv::Controller* c = nullptr;
    if (!c) { try { uv::MsrClient::ensureDaemon(); c = new uv::Controller(); } catch (...) {} }
    return c;
}
}

typedef struct {
    GtkWidget* win;
    GtkWidget* chk_dual_ida;
    GtkWidget* chk_profile1;
    GtkWidget* lbl_msr;
    GtkWidget* lbl_status;
} OpState;

static OpState g_op;

static void op_put(GtkWidget* fixed, GtkWidget* w, int x, int y, int wd, int ht) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    gtk_widget_set_size_request(w, S(wd), S(ht));
}

static GtkWidget* op_group(const char* t) {
    GtkWidget* f = gtk_frame_new(NULL);
    gtk_widget_add_css_class(f, "ts-red-group");
    GtkWidget* l = gtk_label_new(t);
    gtk_widget_add_css_class(l, "ts-red-title");
    gtk_frame_set_label_widget(GTK_FRAME(f), l);
    return f;
}

static void op_load(void) {
    uv::Controller* c = op_ctl();
    if (!c) return;

    char buf[128];

    const auto turbo = c->readTurboEnabled();
    const auto speedstep = c->readSpeedStep();
    const auto bdprochot = c->readBdProchot();

    if (g_op.lbl_msr) {
        snprintf(buf, sizeof(buf),
                 "MSR 0x1A0: Turbo=%s SpeedStep=%s BD-PROCHOT=%s",
                 turbo ? (*turbo ? "on" : "off") : "?",
                 speedstep ? (*speedstep ? "on" : "off") : "?",
                 bdprochot ? (*bdprochot ? "on" : "off") : "?");
        gtk_label_set_text(GTK_LABEL(g_op.lbl_msr), buf);
    }

    if (g_op.chk_dual_ida) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_op.chk_dual_ida),
                                    turbo && *turbo);
    }
    if (g_op.chk_profile1) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_op.chk_profile1),
                                    speedstep && *speedstep);
    }
}

static void op_apply(void) {
    uv::Controller* c = op_ctl();
    if (!c) return;

    char msg[160];
    int okCount = 0, failCount = 0;

    if (g_op.chk_dual_ida) {
        const bool want = gtk_check_button_get_active(
                              GTK_CHECK_BUTTON(g_op.chk_dual_ida));
        try {
            c->setTurboEnabled(want);
            okCount++;
        } catch (...) { failCount++; }
    }
    if (g_op.chk_profile1) {
        const bool want = gtk_check_button_get_active(
                              GTK_CHECK_BUTTON(g_op.chk_profile1));
        try {
            c->setSpeedStep(want);
            okCount++;
        } catch (...) { failCount++; }
    }

    if (failCount == 0) {
        snprintf(msg, sizeof(msg), "已写入 %d 项到 MSR 0x1A0", okCount);
    } else {
        snprintf(msg, sizeof(msg), "%d 项成功，%d 项被硬件拒绝（可能被 BIOS 锁定）",
                 okCount, failCount);
    }
    if (g_op.lbl_status) gtk_label_set_text(GTK_LABEL(g_op.lbl_status), msg);
    op_load();
}

GtkWidget* dlg_options_new(GtkWindow* parent) {
    memset(&g_op, 0, sizeof(g_op));

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Options");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), S(480), S(400));
    g_op.win = win;

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(480), S(400));

    GtkWidget* g1 = op_group("Hardware Options (MSR 0x1A0)");
    op_put(fixed, g1, 7, 2, 466, 120);

    g_op.chk_dual_ida = gtk_check_button_new_with_label("Start Dual IDA (Turbo Mode)");
    op_put(fixed, g_op.chk_dual_ida, 20, 24, 240, 20);

    g_op.chk_profile1 = gtk_check_button_new_with_label("Profile 1 (SpeedStep / EIST)");
    op_put(fixed, g_op.chk_profile1, 20, 46, 240, 20);

    GtkWidget* bd = gtk_check_button_new_with_label("BD PROCHOT（需单独确认）");
    gtk_widget_set_sensitive(bd, FALSE);
    op_put(fixed, bd, 20, 68, 240, 20);

    g_op.lbl_msr = gtk_label_new("");
    gtk_widget_add_css_class(g_op.lbl_msr, "ts-topo");
    gtk_widget_set_halign(g_op.lbl_msr, GTK_ALIGN_START);
    op_put(fixed, g_op.lbl_msr, 20, 92, 440, 18);

    GtkWidget* g2 = op_group("Not applicable on Linux");
    op_put(fixed, g2, 7, 130, 466, 110);

    GtkWidget* note = gtk_label_new(
        "以下 ThrottleStop 选项依赖 Windows 专有实现，本版本不提供：\n"
        "  • 字体 / CPU 温度颜色 / GPU 温度颜色 / MHz 颜色 / Power 颜色\n"
        "  • 皮肤与主题、通知区图标样式\n"
        "  • Windows 全局热键表、告警提示音\n"
        "  • Nvidia / AMD GPU 选择（Linux 下走 DPM，无需此处配置）");
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_widget_set_valign(note, GTK_ALIGN_START);
    op_put(fixed, note, 20, 152, 440, 80);

    g_op.lbl_status = gtk_label_new("");
    gtk_widget_add_css_class(g_op.lbl_status, "ts-topo");
    gtk_widget_set_halign(g_op.lbl_status, GTK_ALIGN_START);
    op_put(fixed, g_op.lbl_status, 7, 250, 466, 20);

    GtkWidget* apply = gtk_button_new_with_label("Apply");
    g_object_set_data(G_OBJECT(apply), "dlgwin", win);
    gtk_widget_add_css_class(apply, "ts-btn");
    op_put(fixed, apply, 200, 280, 84, 26);
    g_signal_connect(apply, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        op_apply();
    }), NULL);

    GtkWidget* ok = gtk_button_new_with_label("OK");
    g_object_set_data(G_OBJECT(ok), "dlgwin", win);
    gtk_widget_add_css_class(ok, "ts-btn");
    op_put(fixed, ok, 292, 280, 84, 26);
    g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        op_apply();
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
    }), NULL);

    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    g_object_set_data(G_OBJECT(cancel), "dlgwin", win);
    gtk_widget_add_css_class(cancel, "ts-btn");
    op_put(fixed, cancel, 384, 280, 84, 26);
    g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
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
    op_load();
    return win;
}
