

#ifndef UV_DIALOGS_H
#define UV_DIALOGS_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget* dlg_turbo_ratio_new(GtkWindow* parent);

GtkWidget* dlg_turbo_power_new(GtkWindow* parent);

GtkWidget* dlg_vf_point_new(GtkWindow* parent);

GtkWidget* dlg_per_core_new(GtkWindow* parent);

GtkWidget* dlg_cstates_new(GtkWindow* parent);

GtkWidget* dlg_options_new(GtkWindow* parent);

GtkWidget* dlg_limit_reasons_new(GtkWindow* parent);

GtkWidget* dlg_fivr_control_new(GtkWindow* parent);

#ifdef __cplusplus
}
#endif

#endif
