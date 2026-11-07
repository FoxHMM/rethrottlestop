

#include "tray.h"

#include <gio/gio.h>
#include "../include/settings.hpp"
#include <stdio.h>
#include <string.h>

#define SNI_PATH "/StatusNotifierItem"

static const char* kIntrospection =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='IconThemePath' type='s' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <method name='Activate'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='ContextMenu'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg name='delta' type='i' direction='in'/>"
    "      <arg name='orientation' type='s' direction='in'/>"
    "    </method>"
    "    <signal name='NewIcon'/>"
    "    <signal name='NewToolTip'/>"
    "    <signal name='NewStatus'>"
    "      <arg name='status' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusConnection* g_conn = NULL;
static guint g_reg_id = 0;
static void tray_show_menu(void);

static GtkWidget* g_window = NULL;

static GVariant* on_get_property(GDBusConnection* conn, const char* sender,
                                 const char* obj_path, const char* iface,
                                 const char* prop, GError** error, gpointer user_data) {
    if (strcmp(prop, "Category") == 0) return g_variant_new_string("ApplicationStatus");
    if (strcmp(prop, "Id") == 0) return g_variant_new_string("ReThrottleStop");
    if (strcmp(prop, "Title") == 0) return g_variant_new_string("ReThrottleStop");
    if (strcmp(prop, "Status") == 0) return g_variant_new_string("Active");
    if (strcmp(prop, "IconName") == 0) return g_variant_new_string("applications-system");
    if (strcmp(prop, "IconPixmap") == 0) {

        const int W = 22, H = 22;
        GVariantBuilder px;
        g_variant_builder_init(&px, G_VARIANT_TYPE("ay"));
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {

                const int dx = x - W/2, dy = y - H/2;
                guint8 a = 0, r = 0, g = 0, b = 0;
                if (dx*dx + dy*dy < 100) {
                    a = 255; r = 20; g = 120; b = 60;
                }
                g_variant_builder_add(&px, "y", (guchar)b);
                g_variant_builder_add(&px, "y", (guchar)g);
                g_variant_builder_add(&px, "y", (guchar)r);
                g_variant_builder_add(&px, "y", a);
            }
        }
        GVariantBuilder arr;
        g_variant_builder_init(&arr, G_VARIANT_TYPE("a(iiay)"));
        g_variant_builder_add(&arr, "(ii@ay)", W, H, g_variant_builder_end(&px));
        return g_variant_builder_end(&arr);
    }
    if (strcmp(prop, "ToolTip") == 0) {
        return g_variant_new("(sa(iiay)ss)", "utilities-system-monitor", NULL,
                             "ReThrottleStop", "CPU 电压/功耗调控");
    }
    if (strcmp(prop, "IconThemePath") == 0) return g_variant_new_string("");
    if (strcmp(prop, "Menu") == 0) return g_variant_new_object_path("/MenuBar");

    if (strcmp(prop, "ItemIsMenu") == 0) return g_variant_new_boolean(TRUE);
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "unknown %s", prop);
    return NULL;
}

static void on_method_call(GDBusConnection* conn, const char* sender,
                           const char* obj_path, const char* iface,
                           const char* method, GVariant* params,
                           GDBusMethodInvocation* inv, gpointer user_data) {
    if (strcmp(method, "Activate") == 0) {

        if (g_window) {
            const gboolean vis = gtk_widget_get_visible(g_window);
            gtk_widget_set_visible(g_window, !vis);
            if (!vis) gtk_window_present(GTK_WINDOW(g_window));
        }
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    if (strcmp(method, "ContextMenu") == 0) {

        tray_show_menu();
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    if (strcmp(method, "SecondaryActivate") == 0 ||
        strcmp(method, "Scroll") == 0) {
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }
    g_dbus_method_invocation_return_value(inv, NULL);
}

static const GDBusInterfaceVTable kVTable = {
    on_method_call, on_get_property, NULL, {0}
};

static GtkWidget* g_menu = nullptr;

static void tray_show_menu(void) {

    static GtkWidget* menuWin = nullptr;
    if (menuWin) {
        gtk_window_destroy(GTK_WINDOW(menuWin));
        menuWin = nullptr;
    }

    menuWin = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(menuWin), FALSE);
    gtk_window_set_title(GTK_WINDOW(menuWin), "ReThrottleStop Menu");
    gtk_window_set_resizable(GTK_WINDOW(menuWin), FALSE);
    gtk_widget_add_css_class(menuWin, "ts-traymenu");
    gtk_window_set_default_size(GTK_WINDOW(menuWin), 200, -1);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);
    gtk_window_set_child(GTK_WINDOW(menuWin), box);

    auto addItem = [&](const char* label, GCallback cb, gpointer data) {
        GtkWidget* b = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(b, "flat");
        gtk_widget_add_css_class(b, "ts-menuitem");
        g_signal_connect(b, "clicked", cb, data);
        gtk_box_append(GTK_BOX(box), b);
    };

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    {
        auto st = uv::loadSettings();
        GtkWidget* lbl = gtk_label_new("配置切换");
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(lbl, "ts-menutitle");
        gtk_box_append(GTK_BOX(box), lbl);

        for (int i = 0; i < 4; ++i) {
            char label[96];
            const char* nm = nullptr;
            char defName[16];
            snprintf(defName, sizeof(defName), "Profile %d", i + 1);
            if (st.valid && !st.mainwin.profileName[i].empty())
                nm = st.mainwin.profileName[i].c_str();

            const bool cur = (st.valid && st.mainwin.activeProfile == i);
            snprintf(label, sizeof(label), "%s %s",
                     cur ? "●" : "○", nm ? nm : defName);

            int* d = static_cast<int*>(g_malloc(2 * sizeof(int)));
            d[0] = i;
            d[1] = 0;
            GtkWidget* b = gtk_button_new_with_label(label);
            gtk_widget_add_css_class(b, "flat");
            gtk_widget_add_css_class(b, "ts-menuitem");
            g_object_set_data_full(G_OBJECT(b), "menuwin", menuWin, nullptr);
            g_object_set_data(G_OBJECT(b), "idx", GINT_TO_POINTER(i));
            g_signal_connect(b, "clicked",
                G_CALLBACK(+[](GtkButton* btn, gpointer) {
                    const int idx = GPOINTER_TO_INT(
                        g_object_get_data(G_OBJECT(btn), "idx"));
                    auto st2 = uv::loadSettings();
                    st2.mainwin.activeProfile = idx;
                    uv::saveSettings(st2);
                    GtkWidget* w = GTK_WIDGET(
                        g_object_get_data(G_OBJECT(btn), "menuwin"));
                    if (w) gtk_window_destroy(GTK_WINDOW(w));
                }), nullptr);
            gtk_box_append(GTK_BOX(box), b);
        }
    }

    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    addItem("退出",
        G_CALLBACK(+[](GtkButton*, gpointer) {
            g_application_quit(g_application_get_default());
        }), nullptr);

    gtk_window_present(GTK_WINDOW(menuWin));

    g_timeout_add(30000, +[](gpointer w) -> gboolean {
        if (w && GTK_IS_WINDOW(w)) {
            gtk_window_destroy(GTK_WINDOW(w));
            menuWin = nullptr;
        }
        return G_SOURCE_REMOVE;
    }, menuWin);
}

static const char* kMenuXml =
    "<node>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <method name='GetLayout'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='as' direction='in'/>"
    "      <arg type='u' direction='out'/>"
    "      <arg type='(ia{sv}av)' direction='out'/>"
    "    </method>"
    "    <method name='GetGroupProperties'>"
    "      <arg type='ai' direction='in'/>"
    "      <arg type='as' direction='in'/>"
    "      <arg type='a(ia{sv})' direction='out'/>"
    "    </method>"
    "    <method name='GetProperty'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='v' direction='out'/>"
    "    </method>"
    "    <method name='Event'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='v' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='EventGroup'>"
    "      <arg type='a(isvu)' direction='in'/>"
    "      <arg type='ai' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShow'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='b' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShowGroup'>"
    "      <arg type='ai' direction='in'/>"
    "      <arg type='ai' direction='out'/>"
    "      <arg type='ai' direction='out'/>"
    "    </method>"
    "    <signal name='ItemsPropertiesUpdated'>"
    "      <arg type='a(ia{sv})'/>"
    "      <arg type='a(ias)'/>"
    "    </signal>"
    "    <signal name='LayoutUpdated'>"
    "      <arg type='u'/>"
    "      <arg type='i'/>"
    "    </signal>"
    "    <signal name='ItemActivationRequested'>"
    "      <arg type='i'/>"
    "      <arg type='u'/>"
    "    </signal>"
    "    <property name='Version' type='u' access='read'/>"
    "    <property name='TextDirection' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconThemePath' type='as' access='read'/>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo* g_menuNode = nullptr;
static guint g_menuRegId = 0;

static GVariant* make_item(int32_t id, const char* label,
                           bool enabled, bool visible,
                           GVariant* children) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));

    if (label) {
        g_variant_builder_add(&props, "{sv}", "label",
                              g_variant_new_string(label));
    }
    g_variant_builder_add(&props, "{sv}", "enabled",
                          g_variant_new_boolean(enabled));
    g_variant_builder_add(&props, "{sv}", "visible",
                          g_variant_new_boolean(visible));

    GVariantBuilder item;
    g_variant_builder_init(&item, G_VARIANT_TYPE("(ia{sv}av)"));
    g_variant_builder_add(&item, "i", id);
    g_variant_builder_add_value(&item, g_variant_builder_end(&props));
    g_variant_builder_add_value(&item, children
                                ? children
                                : g_variant_new_array(G_VARIANT_TYPE_VARIANT,
                                                      nullptr, 0));
    return g_variant_builder_end(&item);
}

static GVariant* make_separator(void) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "type",
                          g_variant_new_string("separator"));
    g_variant_builder_add(&props, "{sv}", "visible",
                          g_variant_new_boolean(TRUE));

    GVariantBuilder item;
    g_variant_builder_init(&item, G_VARIANT_TYPE("(ia{sv}av)"));
    g_variant_builder_add(&item, "i", 10);
    g_variant_builder_add_value(&item, g_variant_builder_end(&props));
    g_variant_builder_add_value(&item, g_variant_new_array(G_VARIANT_TYPE_VARIANT,
                                                           nullptr, 0));
    return g_variant_builder_end(&item);
}

static void profile_labels(char out[4][96]) {
    auto st = uv::loadSettings();
    for (int i = 0; i < 4; ++i) {
        char def[16];
        snprintf(def, sizeof(def), "Profile %d", i + 1);
        const char* nm = (st.valid && !st.mainwin.profileName[i].empty())
                         ? st.mainwin.profileName[i].c_str() : def;
        const bool cur = (st.valid && st.mainwin.activeProfile == i);
        snprintf(out[i], 96, "%s %s", cur ? "●" : "○", nm);
    }
}

static void on_menu_method(GDBusConnection*, const char*, const char*,
                           const char* iface, const char* method,
                           GVariant* params, GDBusMethodInvocation* inv,
                           gpointer) {
    if (strcmp(method, "GetLayout") == 0) {

        GVariantBuilder kids;
        g_variant_builder_init(&kids, G_VARIANT_TYPE("av"));

        char labels[4][96];
        profile_labels(labels);
        for (int i = 0; i < 4; ++i) {
            g_variant_builder_add(&kids, "v",
                make_item(1 + i, labels[i], true, true, nullptr));
        }
        g_variant_builder_add(&kids, "v", make_separator());
        g_variant_builder_add(&kids, "v",
            make_item(20, "退出", true, true, nullptr));

        GVariant* root = make_item(0, nullptr, true, true,
                                   g_variant_builder_end(&kids));

        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(u@(ia{sv}av))", 1u, root));
        return;
    }

    if (strcmp(method, "GetGroupProperties") == 0) {
        GVariantBuilder out;
        g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})"));

        char labels[4][96];
        profile_labels(labels);
        for (int i = 0; i < 4; ++i) {
            GVariantBuilder p;
            g_variant_builder_init(&p, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&p, "{sv}", "label",
                                  g_variant_new_string(labels[i]));
            g_variant_builder_add(&p, "{sv}", "enabled",
                                  g_variant_new_boolean(TRUE));
            g_variant_builder_add(&p, "{sv}", "visible",
                                  g_variant_new_boolean(TRUE));
            g_variant_builder_add(&out, "(ia{sv})", 1 + i,
                                  g_variant_builder_end(&p));
        }
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(a(ia{sv}))", g_variant_builder_end(&out)));
        return;
    }

    if (strcmp(method, "GetProperty") == 0) {
        int32_t id = 0;
        const char* name = nullptr;
        g_variant_get(params, "(i&s)", &id, &name);
        if (strcmp(name, "label") == 0 && id >= 1 && id <= 4) {
            char labels[4][96];
            profile_labels(labels);
            g_dbus_method_invocation_return_value(inv,
                g_variant_new("(v)", g_variant_new_string(labels[id - 1])));
        } else if (strcmp(name, "label") == 0 && id == 20) {
            g_dbus_method_invocation_return_value(inv,
                g_variant_new("(v)", g_variant_new_string("退出")));
        } else {
            g_dbus_method_invocation_return_value(inv,
                g_variant_new("(v)", g_variant_new_boolean(TRUE)));
        }
        return;
    }

    if (strcmp(method, "Event") == 0) {
        int32_t id = 0;
        const char* event = nullptr;
        g_variant_get(params, "(i&svu)", &id, &event, nullptr, nullptr);
        if (strcmp(event, "clicked") == 0) {
            if (id >= 1 && id <= 4) {

                auto st = uv::loadSettings();
                st.mainwin.activeProfile = id - 1;
                uv::saveSettings(st);
            } else if (id == 20) {
                g_application_quit(g_application_get_default());
            }
        }
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (strcmp(method, "EventGroup") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(ai)", g_variant_new_array(G_VARIANT_TYPE_INT32,
                                                      nullptr, 0)));
        return;
    }

    if (strcmp(method, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(b)", FALSE));
        return;
    }

    if (strcmp(method, "AboutToShowGroup") == 0) {
        GVariantBuilder a, b;
        g_variant_builder_init(&a, G_VARIANT_TYPE("ai"));
        g_variant_builder_init(&b, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(aiai)", g_variant_builder_end(&a),
                          g_variant_builder_end(&b)));
        return;
    }

    g_dbus_method_invocation_return_value(inv, nullptr);
}

static GVariant* on_menu_get_property(GDBusConnection*, const char*,
                                      const char* obj, const char* iface,
                                      const char* prop, GError**,
                                      gpointer) {
    if (strcmp(prop, "Version") == 0) return g_variant_new_uint32(3);
    if (strcmp(prop, "TextDirection") == 0)
        return g_variant_new_string("ltr");
    if (strcmp(prop, "Status") == 0) return g_variant_new_string("normal");
    if (strcmp(prop, "IconThemePath") == 0)
        return g_variant_new_strv(nullptr, 0);
    return nullptr;
}

static const GDBusInterfaceVTable kMenuVTable = {
    on_menu_method, on_menu_get_property, nullptr, {0}
};

static bool register_dbusmenu(GDBusConnection* conn) {
    if (g_menuRegId != 0) return true;

    GError* err = nullptr;
    if (!g_menuNode) {
        g_menuNode = g_dbus_node_info_new_for_xml(kMenuXml, &err);
        if (!g_menuNode) {
            g_printerr("tray: menu xml parse failed: %s\n",
                       err ? err->message : "?");
            if (err) g_error_free(err);
            return false;
        }
    }

    g_menuRegId = g_dbus_connection_register_object(
        conn, "/MenuBar", g_menuNode->interfaces[0], &kMenuVTable,
        nullptr, nullptr, &err);
    if (g_menuRegId == 0) {
        g_printerr("tray: menu register failed: %s\n",
                   err ? err->message : "?");
        if (err) g_error_free(err);
        return false;
    }
    return true;
}

int tray_init(GtkWidget* window) {
    g_window = window;

    GError* err = NULL;
    g_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!g_conn) {
        g_printerr("tray: cannot connect to session bus: %s\n",
                   err ? err->message : "?");
        if (err) g_error_free(err);
        return 0;
    }

    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospection, &err);
    if (!node) {
        g_printerr("tray: bad introspection: %s\n", err ? err->message : "?");
        return 0;
    }

    g_reg_id = g_dbus_connection_register_object(
        g_conn, SNI_PATH, node->interfaces[0], &kVTable, NULL, NULL, &err);

    register_dbusmenu(g_conn);
    g_dbus_node_info_unref(node);

    if (g_reg_id == 0) {
        g_printerr("tray: register failed: %s\n", err ? err->message : "?");
        return 0;
    }

    GVariant* reply = g_dbus_connection_call_sync(
        g_conn,
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem",

        g_variant_new("(s)", SNI_PATH),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        3000,
        NULL,
        &err);

    if (!reply) {
        g_printerr("tray: watcher register failed: %s\n",
                   err ? err->message : "?");
        if (err) g_error_free(err);
        return 0;
    }
    g_variant_unref(reply);
    return 1;
}

void tray_dispose(void) {
    if (g_conn && g_reg_id) {
        g_dbus_connection_unregister_object(g_conn, g_reg_id);
        g_reg_id = 0;
    }
    if (g_conn) {
        g_object_unref(g_conn);
        g_conn = NULL;
    }
}
