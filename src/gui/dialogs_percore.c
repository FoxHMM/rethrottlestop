

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
uv::Controller* pc_ctl() {
    static uv::Controller* c = nullptr;
    if (!c) { try { uv::MsrClient::ensureDaemon(); c = new uv::Controller(); } catch (...) {} }
    return c;
}
}

typedef struct {
    GtkWidget* win;
    GtkWidget* edit_set[9];
    GtkWidget* edit_def[9];
    GtkWidget* edit_now[9];
    GtkWidget* lbl_status;
} PcState;

static PcState g_pc;

static void pc_put(GtkWidget* fixed, GtkWidget* w, int x, int y, int wd, int ht) {
    gtk_fixed_put(GTK_FIXED(fixed), w, S(x), S(y));
    gtk_widget_set_size_request(w, S(wd), S(ht));
}

static GtkWidget* pc_edit(const char* t, gboolean ro) {
    GtkWidget* e = gtk_entry_new();
    if (t) gtk_editable_set_text(GTK_EDITABLE(e), t);
    gtk_widget_add_css_class(e, ro ? "ts-locked" : "ts-edit");
    if (ro) {
        gtk_editable_set_editable(GTK_EDITABLE(e), FALSE);
        gtk_widget_set_can_focus(e, FALSE);
    }
    return e;
}

static void pc_load(void) {
    uv::Controller* c = pc_ctl();
    if (!c) return;

    const auto cur = c->readPerCoreRatios();
    const auto def = c->readPerCoreDefaults();
    char buf[16];

    for (int i = 0; i < 9; ++i) {

        if (i == 8) {
            int allDef = 0;
            for (size_t k = 0; k < def.size(); ++k) {
                if (def[k] > allDef) allDef = def[k];
            }
            snprintf(buf, sizeof(buf), "%d", allDef);
            if (g_pc.edit_def[8])
                gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_def[8]), buf);
            snprintf(buf, sizeof(buf), "%d", allDef);
            if (g_pc.edit_set[8])
                gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_set[8]), buf);
            if (g_pc.edit_now[8])
                gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_now[8]), "-");
            continue;
        }

        if (static_cast<size_t>(i) < def.size() && g_pc.edit_def[i]) {
            snprintf(buf, sizeof(buf), "%d", def[i]);
            gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_def[i]), buf);
            gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_set[i]), buf);
        }
        if (static_cast<size_t>(i) < cur.size() && g_pc.edit_now[i]) {
            if (cur[i] > 0) {
                snprintf(buf, sizeof(buf), "%d", cur[i]);
                gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_now[i]), buf);
            } else {
                gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_now[i]), "-");
            }
        }
    }

    if (g_pc.lbl_status) {
        snprintf(buf, sizeof(buf),
                 "已读取 %zu 核当前倍频 / BIOS 八档默认值", cur.size());
        gtk_label_set_text(GTK_LABEL(g_pc.lbl_status), buf);
    }
}

static void pc_apply(void) {
    uv::Controller* c = pc_ctl();
    if (!c) return;

    std::array<int, 8> ratios;
    ratios.fill(-1);
    for (int i = 0; i < 8; ++i) {
        if (!g_pc.edit_set[i]) continue;
        const char* t = gtk_editable_get_text(GTK_EDITABLE(g_pc.edit_set[i]));
        if (t && *t) {
            const int v = atoi(t);
            if (v > 0 && v < 256) ratios[i] = v;
        }
    }

    char msg[160];
    try {
        c->setTurboRatios(ratios);
        snprintf(msg, sizeof(msg), "已写入 MSR 0x1AD（八档倍频表）");
    } catch (...) {
        snprintf(msg, sizeof(msg), "写入被硬件拒绝");
    }
    if (g_pc.lbl_status) gtk_label_set_text(GTK_LABEL(g_pc.lbl_status), msg);
    pc_load();
}

GtkWidget* dlg_per_core_new(GtkWindow* parent) {
    memset(&g_pc, 0, sizeof(g_pc));

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Per Core Ratio Limits");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), S(270), S(300));
    g_pc.win = win;

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, S(270), S(300));

    GtkWidget* grp = gtk_frame_new(NULL);
    gtk_widget_add_css_class(grp, "ts-red-group");
    GtkWidget* gl = gtk_label_new("Per Core Ratio Limits");
    gtk_widget_add_css_class(gl, "ts-red-title");
    gtk_frame_set_label_widget(GTK_FRAME(grp), gl);
    pc_put(fixed, grp, 9, 6, 247, 215);

    GtkWidget* h1 = gtk_label_new("Limit");
    gtk_widget_add_css_class(h1, "ts-table-head");
    pc_put(fixed, h1, 24, 19, 48, 14);
    GtkWidget* h2 = gtk_label_new("Defaults");
    gtk_widget_add_css_class(h2, "ts-table-head");
    pc_put(fixed, h2, 82, 19, 48, 14);
    GtkWidget* h3 = gtk_label_new("Now");
    gtk_widget_add_css_class(h3, "ts-table-head");
    pc_put(fixed, h3, 140, 19, 48, 14);

    for (int i = 0; i < 9; ++i) {
        const int y = 37 + i * 20;
        g_pc.edit_set[i] = pc_edit("", FALSE);
        pc_put(fixed, g_pc.edit_set[i], 24, y, 48, 17);

        g_pc.edit_def[i] = pc_edit("-", TRUE);
        pc_put(fixed, g_pc.edit_def[i], 82, y, 48, 17);

        g_pc.edit_now[i] = pc_edit("-", TRUE);
        pc_put(fixed, g_pc.edit_now[i], 140, y, 48, 17);
    }

    g_pc.lbl_status = gtk_label_new("");
    gtk_widget_add_css_class(g_pc.lbl_status, "ts-topo");
    gtk_widget_set_halign(g_pc.lbl_status, GTK_ALIGN_START);
    pc_put(fixed, g_pc.lbl_status, 9, 226, 247, 18);

    GtkWidget* use = gtk_button_new_with_label("Use Limits");
    g_object_set_data(G_OBJECT(use), "dlgwin", win);
    gtk_widget_add_css_class(use, "ts-btn");
    pc_put(fixed, use, 14, 246, 76, 22);
    g_signal_connect(use, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        pc_apply();
    }), NULL);

    GtkWidget* mx = gtk_button_new_with_label("Max");
    g_object_set_data(G_OBJECT(mx), "dlgwin", win);
    gtk_widget_add_css_class(mx, "ts-btn");
    pc_put(fixed, mx, 95, 246, 77, 22);
    g_signal_connect(mx, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {

        uv::Controller* c = pc_ctl();
        if (!c) return;
        const auto bios = c->readBiosTurboRatios();
        int maxv = 0;
        for (int i = 0; i < 8; ++i) if (bios[i] > maxv) maxv = bios[i];
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", maxv);
        for (int i = 0; i < 9; ++i) {
            if (g_pc.edit_set[i])
                gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_set[i]), buf);
        }
    }), NULL);

    GtkWidget* dflt = gtk_button_new_with_label("Defaults");
    g_object_set_data(G_OBJECT(dflt), "dlgwin", win);
    gtk_widget_add_css_class(dflt, "ts-btn");
    pc_put(fixed, dflt, 176, 246, 77, 22);
    g_signal_connect(dflt, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        for (int i = 0; i < 9; ++i) {
            const char* d = gtk_editable_get_text(
                GTK_EDITABLE(g_pc.edit_def[i]));
            if (g_pc.edit_set[i] && d) {
                gtk_editable_set_text(GTK_EDITABLE(g_pc.edit_set[i]), d);
            }
        }
    }), NULL);

    GtkWidget* ok = gtk_button_new_with_label("OK");
    g_object_set_data(G_OBJECT(ok), "dlgwin", win);
    gtk_widget_add_css_class(ok, "ts-btn");
    pc_put(fixed, ok, 14, 272, 77, 22);
    g_signal_connect(ok, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        pc_apply();
        GtkWidget* w2 = GTK_WIDGET(g_object_get_data(G_OBJECT(b), "dlgwin"));
            if (w2) gtk_window_destroy(GTK_WINDOW(w2));
    }), NULL);

    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    g_object_set_data(G_OBJECT(cancel), "dlgwin", win);
    gtk_widget_add_css_class(cancel, "ts-btn");
    pc_put(fixed, cancel, 95, 272, 77, 22);
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
    pc_load();
    return win;
}
