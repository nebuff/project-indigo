#include "menu.h"

#include <string.h>

#include <gtk/gtk.h>

#include "pages.h"
#include "util.h"

#define MAX_CLOSED 10
#define MAX_HISTORY_ROWS 25
#define MAX_LABEL_CHARS 64

typedef struct {
    char *url;
    char *title;
} ClosedTab;

struct _Menu {
    GtkWidget *popover;
    History *history;
    GPtrArray *closed;
    MenuNavigateFunc navigate;
    MenuOpenTabFunc open_tab;
    MenuSettingsFunc open_settings;
    gpointer user_data;
};

static void
rebuild_model(Menu *menu);

/* Menu labels are single line, so clip very long page titles. */
static char *
label_for(const char *title, const char *url)
{
    char *host = uri_host(url);
    const char *text = (title != NULL && *title != '\0')
        ? title
        : (host != NULL ? host : url);
    char *label;

    if (g_utf8_strlen(text, -1) > MAX_LABEL_CHARS) {
        char *cut = g_utf8_substring(text, 0, MAX_LABEL_CHARS - 1);

        label = g_strconcat(cut, "\xe2\x80\xa6", NULL);
        g_free(cut);
    } else {
        label = g_strdup(text);
    }

    g_free(host);
    return label;
}

static void
append_target_item(GMenu *submenu, const char *label, const char *action,
                   GVariant *target)
{
    GMenuItem *item = g_menu_item_new(label, NULL);

    g_menu_item_set_action_and_target_value(item, action, target);
    g_menu_append_item(submenu, item);
    g_object_unref(item);
}

static void
on_settings_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Menu *menu = data;

    (void)action;
    (void)parameter;

    gtk_popover_popdown(GTK_POPOVER(menu->popover));
    menu->open_settings(menu->user_data);
}

static void
on_clear_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Menu *menu = data;

    (void)action;
    (void)parameter;

    /* The menu is rebuilt the next time it opens: changing the model from
     * inside one of its own items would destroy the item being activated. */
    history_clear(menu->history);
    gtk_popover_popdown(GTK_POPOVER(menu->popover));
}

static void
on_open_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Menu *menu = data;

    (void)action;

    gtk_popover_popdown(GTK_POPOVER(menu->popover));
    menu->navigate(g_variant_get_string(parameter, NULL), menu->user_data);
}

static void
on_reopen_action(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Menu *menu = data;
    guint index = g_variant_get_uint32(parameter);

    (void)action;

    if (index >= menu->closed->len)
        return;

    ClosedTab *tab = g_ptr_array_index(menu->closed, index);

    gtk_popover_popdown(GTK_POPOVER(menu->popover));
    menu->open_tab(tab->url, menu->user_data);
}

static GMenu *
closed_section_new(Menu *menu)
{
    GMenu *section = g_menu_new();

    for (guint i = 0; i < menu->closed->len; i++) {
        ClosedTab *tab = g_ptr_array_index(menu->closed, i);
        char *label = label_for(tab->title, tab->url);

        append_target_item(section, label, "menu.reopen", g_variant_new_uint32(i));
        g_free(label);
    }

    return section;
}

static GMenu *
history_section_new(Menu *menu)
{
    GMenu *section = g_menu_new();
    int count = MIN(history_count(menu->history), MAX_HISTORY_ROWS);

    for (int i = 0; i < count; i++) {
        const HistoryEntry *entry = history_get(menu->history, i);
        char *label = label_for(entry->title, entry->url);

        append_target_item(section, label, "menu.open",
                           g_variant_new_string(entry->url));
        g_free(label);
    }

    return section;
}

static void
rebuild_model(Menu *menu)
{
    GMenu *root = g_menu_new();
    GMenu *history = g_menu_new();
    int count = history_count(menu->history);

    if (menu->closed->len == 0 && count == 0) {
        GMenuItem *empty = g_menu_item_new("No history yet", NULL);

        g_menu_append_item(history, empty);
        g_object_unref(empty);
    } else {
        if (menu->closed->len > 0) {
            GMenu *section = closed_section_new(menu);

            g_menu_append_section(history, "Recently closed tabs",
                                  G_MENU_MODEL(section));
            g_object_unref(section);
        }

        GMenu *actions = g_menu_new();

        g_menu_append(actions, "Clear History", "menu.clear");
        g_menu_append_section(history, NULL, G_MENU_MODEL(actions));
        g_object_unref(actions);

        if (count > 0) {
            GMenu *section = history_section_new(menu);

            g_menu_append_section(history, "Recent history", G_MENU_MODEL(section));
            g_object_unref(section);
        }
    }

    g_menu_append(root, "Settings", "menu.settings");
    g_menu_append_submenu(root, "History", G_MENU_MODEL(history));
    gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(menu->popover), G_MENU_MODEL(root));

    g_object_unref(history);
    g_object_unref(root);
}

static void
on_popover_visible(GObject *object, GParamSpec *pspec, Menu *menu)
{
    (void)pspec;

    if (!gtk_widget_get_visible(GTK_WIDGET(object)))
        return;

    /* The menu always opens on its root section, with fresh history. */
    rebuild_model(menu);
}

Menu *
menu_new(GtkWidget *button,
         History *history,
         MenuNavigateFunc navigate,
         MenuOpenTabFunc open_tab,
         MenuSettingsFunc open_settings,
         gpointer user_data)
{
    static const GActionEntry entries[] = {
        { .name = "settings", .activate = on_settings_action },
        { .name = "clear", .activate = on_clear_action },
        { .name = "open", .activate = on_open_action, .parameter_type = "s" },
        { .name = "reopen", .activate = on_reopen_action, .parameter_type = "u" },
    };

    Menu *menu = g_new0(Menu, 1);

    menu->history = history;
    menu->navigate = navigate;
    menu->open_tab = open_tab;
    menu->open_settings = open_settings;
    menu->user_data = user_data;
    menu->closed = g_ptr_array_new_with_free_func((GDestroyNotify)NULL);

    GSimpleActionGroup *actions = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(actions), entries,
                                    G_N_ELEMENTS(entries), menu);
    gtk_widget_insert_action_group(button, "menu", G_ACTION_GROUP(actions));
    g_object_unref(actions);

    menu->popover = gtk_popover_menu_new_from_model(NULL);
    gtk_popover_set_position(GTK_POPOVER(menu->popover), GTK_POS_BOTTOM);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), menu->popover);
    g_signal_connect(menu->popover, "notify::visible",
                     G_CALLBACK(on_popover_visible), menu);

    rebuild_model(menu);
    return menu;
}

void
menu_free(Menu *menu)
{
    if (menu == NULL)
        return;

    for (guint i = 0; i < menu->closed->len; i++) {
        ClosedTab *tab = g_ptr_array_index(menu->closed, i);

        g_free(tab->url);
        g_free(tab->title);
        g_free(tab);
    }

    g_ptr_array_unref(menu->closed);
    g_free(menu);
}

void
menu_record_closed(Menu *menu, const char *url, const char *title)
{
    /* Internal pages and blank documents are not worth restoring. */
    if (menu == NULL || url == NULL || *url == '\0' || page_is_internal(url) ||
        g_str_has_prefix(url, "about:") || g_str_has_prefix(url, "data:"))
        return;

    ClosedTab *tab = g_new0(ClosedTab, 1);

    tab->url = g_strdup(url);
    tab->title = g_strdup(title != NULL && *title != '\0' ? title : url);

    g_ptr_array_insert(menu->closed, 0, tab);

    if (menu->closed->len > MAX_CLOSED) {
        ClosedTab *last = g_ptr_array_index(menu->closed, menu->closed->len - 1);

        g_free(last->url);
        g_free(last->title);
        g_free(last);
        g_ptr_array_remove_index(menu->closed, menu->closed->len - 1);
    }
}
