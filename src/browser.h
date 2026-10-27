#pragma once

#include <gtk/gtk.h>

struct _Browser;
typedef struct _Browser Browser;

GtkWidget *browser_window_new(GtkApplication *application);
void browser_show(Browser *browser);
void browser_activate(GtkApplication *app, gpointer G_GNUC_UNUSED user_data);
