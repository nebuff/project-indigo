#pragma once

#include <gtk/gtk.h>

#include "history.h"

typedef struct _Menu Menu;

typedef void (*MenuNavigateFunc)(const char *uri, gpointer user_data);
typedef void (*MenuOpenTabFunc)(const char *uri, gpointer user_data);
typedef void (*MenuSettingsFunc)(gpointer user_data);

/* The toolbar menu button: a root menu with Settings and History, plus a
 * second panel listing recently closed tabs and recent history. */
Menu *menu_new(GtkWidget *button,
               History *history,
               MenuNavigateFunc navigate,
               MenuOpenTabFunc open_tab,
               MenuSettingsFunc open_settings,
               gpointer user_data);
void menu_free(Menu *menu);
void menu_record_closed(Menu *menu, const char *url, const char *title);
