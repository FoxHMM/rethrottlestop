

#include "dialogs.h"
#include "i18n.h"

#include "../../include/undervolt.hpp"
#include "../../include/msr_daemon.hpp"
#include "../../include/settings.hpp"
#include "../../include/ts_spec.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

extern double ts_scale(void);
#define S(v) ((int)((v) * ts_scale()))

namespace {
uv::Controller* vf_ctl() {
    static uv::Controller* c = nullptr;
    if (!c) { try { uv::MsrClient::ensureDaemon(); c = new uv::Controller(); } catch (...) {} }
    return c;
}
}

typedef struct {
    GtkWidget* win;

    GtkWidget* cells[11][6];
    GtkWidget* lbl_status;
    int domain;
} VfState;

static VfState g_vf;

static void vf_put(GtkWidget* fixed, GtkWidget* w, int x, int y, int wd, int ht) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    gtk_widget_set_size_request(w, S(wd), S(ht));
}

static GtkWidget* vf_red_group(const char* title) {
    GtkWidget* f = gtk_frame_new(NULL);
    gtk_widget_add_css_class(f, "ts-red-group");
    GtkWidget* lbl = gtk_label_new(title);
    gtk_widget_add_css_class(lbl, "ts-red-title");
    gtk_frame_set_label_widget(GTK_FRAME(f), lbl);
    return f;
}

static GtkWidget* vf_edit(const char* text) {
    GtkWidget* e = gtk_entry_new();
    if (text) gtk_editable_set_text(GTK_EDITABLE(e), text);
    gtk_widget_add_css_class(e, "ts-edit");
    return e;
}

static void vf_load(void) {
    uv::Controller* c = vf_ctl();
    if (!c) return;

    char buf[32];
    int loaded = 0;

    for (int row = 0; row < 11; ++row) {

        if (g_vf.cells[row][0]) {
            snprintf(buf, sizeof(buf), "P%d", row);
            gtk_editable_set_text(GTK_EDITABLE(g_vf.cells[row][0]), buf);
            gtk_editable_set_editable(GTK_EDITABLE(g_vf.cells[row][0]), FALSE);
        }

        if (g_vf.cells[row][1]) {
            gtk_editable_set_text(GTK_EDITABLE(g_vf.cells[row][1]), "-");
            gtk_editable_set_editable(GTK_EDITABLE(g_vf.cells[row][1]), FALSE);
        }

        for (int prof = 0; prof < 4; ++prof) {
            GtkWidget* w = g_vf.cells[row][prof + 2];
            if (!w) continue;
            const auto v = c->getVfPoint(static_cast<uv::Plane>(g_vf.domain),
                                          prof, row);
            if (v) {
                snprintf(buf, sizeof(buf), "%+d", *v);
                loaded++;
            } else {
                snprintf(buf, sizeof(buf), "-");
            }
            gtk_editable_set_text(GTK_EDITABLE(w), buf);
            struct timespec ts = {0, 20 * 1000 * 1000};
            nanosleep(&ts, 0);
        }
    }

    if (g_vf.lbl_status) {
        if (loaded > 0) {
            snprintf(buf, sizeof(buf),
                     "已从 FIVR 邮箱读取 %d 个 V/F 点（域：%s）",
                     loaded, g_vf.domain == 0 ? "CPU Core" : "CPU Cache");
        } else {
            snprintf(buf, sizeof(buf),
                     "FIVR 邮箱未返回 V/F 数据（本平台该命令无响应）");
        }
        gtk_label_set_text(GTK_LABEL(g_vf.lbl_status), buf);
    }
}

static void vf_apply(void) {
    uv::Controller* c = vf_ctl();
    if (!c) return;

    int written = 0, failed = 0;
    for (int row = 0; row < 11; ++row) {
        GtkWidget* w = g_vf.cells[row][2];
        if (!w) continue;
        const char* txt = gtk_editable_get_text(GTK_EDITABLE(w));
        if (!txt || !*txt || strcmp(txt, "-") == 0) continue;

        const int mv = atoi(txt);
        if (mv < -999 || mv > 999) continue;

        try {
            c->setVfPoint(static_cast<uv::Plane>(g_vf.domain), 0, row, mv);
            written++;
        } catch (...) {
            failed++;
        }
    }

    char buf[160];
    if (failed == 0 && written > 0) {
        snprintf(buf, sizeof(buf), "已写入 %d 个 V/F 点", written);
    } else if (written > 0) {
        snprintf(buf, sizeof(buf), "写入 %d 个成功，%d 个被硬件拒绝",
                 written, failed);
    } else {
        snprintf(buf, sizeof(buf),
                 "写入失败：本平台 FIVR 邮箱不支持 V/F 点写入");
    }
    if (g_vf.lbl_status) gtk_label_set_text(GTK_LABEL(g_vf.lbl_status), buf);

    vf_load();
}

GtkWidget* dlg_vf_point_new(GtkWindow* parent) {
    memset(&g_vf, 0, sizeof(g_vf));
    g_vf.domain = 0;

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "V/F Point Offset Tuner");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), S(540), S(400));
    g_vf.win = win;

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(540), S(400));

    GtkWidget* g1 = vf_red_group("Core V/F Points");
    vf_put(fixed, g1, 10, 6, 248, 255);

    const char* heads[] = {"Point", "mV", "Prof 1", "Prof 2", "Prof 3", "Prof 4"};
    static const int kHeadX[] = {20, 68, 112, 152, 192, 232};
    static const int kHeadW[] = {44, 40, 38, 38, 38, 38};
    for (int c = 0; c < 6; ++c) {
        GtkWidget* h = gtk_label_new(heads[c]);
        gtk_widget_add_css_class(h, "ts-table-head");
        gtk_widget_set_halign(h, GTK_ALIGN_CENTER);
        vf_put(fixed, h, kHeadX[c], 28, kHeadW[c], 16);
    }

    for (int row = 0; row < 11; ++row) {
        const int y = 48 + row * 19;
        for (int col = 0; col < 6; ++col) {
            GtkWidget* e = vf_edit("-");
            if (col == 0) {
                gtk_editable_set_editable(GTK_EDITABLE(e), FALSE);
                gtk_widget_add_css_class(e, "ts-locked");
            }
            g_vf.cells[row][col] = e;
            vf_put(fixed, e, kHeadX[col], y, kHeadW[col], 17);
        }
    }

    GtkWidget* g2 = vf_red_group("Range (+/- 999 mV)");
    vf_put(fixed, g2, 270, 6, 248, 120);

    GtkWidget* lbl_range = gtk_label_new("有效范围：-999 ~ +999 mV\n"
                                         "编码：11 位补码，1.024 缩放\n"
                                         "（实测往返全对，含边界）");
    gtk_widget_set_halign(lbl_range, GTK_ALIGN_START);
    vf_put(fixed, lbl_range, 282, 30, 224, 80);

    GtkWidget* g3 = vf_red_group("Domain");
    vf_put(fixed, g3, 270, 134, 248, 80);

    GtkWidget* rb_core = gtk_check_button_new_with_label("CPU Core");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(rb_core), TRUE);
    vf_put(fixed, rb_core, 282, 154, 110, 18);
    GtkWidget* rb_cache = gtk_check_button_new_with_label("CPU Cache");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(rb_cache),
                               GTK_CHECK_BUTTON(rb_core));
    vf_put(fixed, rb_cache, 282, 176, 110, 18);

    g_signal_connect(rb_core, "toggled", G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
        if (!gtk_check_button_get_active(b)) return;
        g_vf.domain = 0;
        vf_load();
    }), NULL);
    g_signal_connect(rb_cache, "toggled", G_CALLBACK(+[](GtkCheckButton* b, gpointer) {
        if (!gtk_check_button_get_active(b)) return;
        g_vf.domain = 2;
        vf_load();
    }), NULL);

    g_vf.lbl_status = gtk_label_new("");
    gtk_widget_add_css_class(g_vf.lbl_status, "ts-topo");
    gtk_widget_set_halign(g_vf.lbl_status, GTK_ALIGN_START);
    vf_put(fixed, g_vf.lbl_status, 10, 268, 508, 20);

    GtkWidget* sync = gtk_button_new_with_label("Sync");
    g_object_set_data(G_OBJECT(sync), "dlgwin", win);
    gtk_widget_add_css_class(sync, "ts-btn");
    vf_put(fixed, sync, 10, 300, 90, 26);
    g_signal_connect(sync, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {

        for (int row = 0; row < 11; ++row) {
            const char* src = gtk_editable_get_text(
                GTK_EDITABLE(g_vf.cells[row][2]));
            for (int p = 3; p <= 5; ++p) {
                if (g_vf.cells[row][p]) {
                    gtk_editable_set_text(GTK_EDITABLE(g_vf.cells[row][p]), src);
                }
            }
        }
    }), NULL);

    GtkWidget* clear = gtk_button_new_with_label("Clear");
    g_object_set_data(G_OBJECT(clear), "dlgwin", win);
    gtk_widget_add_css_class(clear, "ts-btn");
    vf_put(fixed, clear, 108, 300, 90, 26);
    g_signal_connect(clear, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        for (int row = 0; row < 11; ++row) {
            for (int p = 2; p <= 5; ++p) {
                if (g_vf.cells[row][p]) {
                    gtk_editable_set_text(GTK_EDITABLE(g_vf.cells[row][p]), "0");
                }
            }
        }
    }), NULL);

    GtkWidget* apply = gtk_button_new_with_label("Apply");
    g_object_set_data(G_OBJECT(apply), "dlgwin", win);
    gtk_widget_add_css_class(apply, "ts-btn");
    vf_put(fixed, apply, 260, 300, 90, 26);
    g_signal_connect(apply, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        vf_apply();
    }), NULL);

    GtkWidget* ok = gtk_button_new_with_label("OK");
    g_object_set_data(G_OBJECT(ok), "dlgwin", win);
    gtk_widget_add_css_class(ok, "ts-btn");
    vf_put(fixed, ok, 358, 300, 70, 26);
    g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        vf_apply();
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
    }), NULL);

    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    g_object_set_data(G_OBJECT(cancel), "dlgwin", win);
    gtk_widget_add_css_class(cancel, "ts-btn");
    vf_put(fixed, cancel, 436, 300, 90, 26);
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
    vf_load();

    return win;
}
