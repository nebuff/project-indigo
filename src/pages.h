#pragma once

#include <gtk/gtk.h>

#include "history.h"

/* Settings is rendered as HTML with an indigo:// base URI. */
#define PAGE_SETTINGS "indigo://settings"

typedef void (*PagesOpenFunc)(const char *uri, gpointer user_data);

gboolean page_is_internal(const char *uri);
char *pages_settings_html(void);

/* The new tab page: branding plus a grid of the most visited sites, or a few
 * suggestions until there is history. */
GtkWidget *pages_new_tab_new(History *history, PagesOpenFunc open, gpointer user_data);
