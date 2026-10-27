#pragma once

#include <gtk/gtk.h>

typedef struct {
    /* The notebook page: the new tab page and the web view in a stack. */
    GtkWidget *stack;
    GtkWidget *view;
    GtkWidget *label;
    GdkTexture *icon;
    /* The new tab page sits behind the current page in this tab's history. */
    gboolean start_in_history;
} Tab;

Tab *tab_new(GtkWidget *new_tab_page);
gboolean tab_showing_web(Tab *tab);
void tab_show_web(Tab *tab);
void tab_free(Tab *tab);
