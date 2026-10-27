#pragma once

#include <gtk/gtk.h>

typedef struct _Omnibox Omnibox;

typedef void (*OmniboxOpenTabFunc)(GtkWidget *page, gpointer user_data);
typedef void (*OmniboxSearchFunc)(const char *query, gpointer user_data);

/* The popover attached to the address bar: open tabs, then recent searches. */
Omnibox *omnibox_new(GtkWidget *entry,
                     GtkNotebook *notebook,
                     OmniboxOpenTabFunc open_tab,
                     OmniboxSearchFunc search,
                     gpointer user_data);
void omnibox_free(Omnibox *omnibox);
void omnibox_close(Omnibox *omnibox);
void omnibox_record_search(Omnibox *omnibox, const char *query);
