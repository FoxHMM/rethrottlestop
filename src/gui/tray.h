
#ifndef UV_TRAY_H
#define UV_TRAY_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

int tray_init(GtkWidget* window);
void tray_dispose(void);

#ifdef __cplusplus
}
#endif
#endif
