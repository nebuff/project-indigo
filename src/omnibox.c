#include "omnibox.h"
#include "search_history.h"
#include "tab.h"
#include "util.h"

#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

struct _Omnibox {
    GtkWidget *entry;
    GtkWidget *popover;
    GtkWidget *list;
    GtkNotebook *notebook;
    OmniboxOpenTabFunc open_tab;
    OmniboxSearchFunc search;
    gpointer user_data;
    SearchHistory *history;
    gboolean open;
    gboolean closing;
    guint closing_id;
};

static void
rebuild(Omnibox *omnibox);

static void
clear_box(GtkWidget *box)
{
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child(box)) != NULL)
        gtk_box_remove(GTK_BOX(box), child);
}

static gboolean
matches(const char *needle, const char *haystack)
{
    if (haystack == NULL)
        return FALSE;

    char *folded = g_utf8_casefold(haystack, -1);
    gboolean found = strstr(folded, needle) != NULL;
    g_free(folded);
    return found;
}

static char *
current_uri(Omnibox *omnibox)
{
    int page = gtk_notebook_get_current_page(omnibox->notebook);

    if (page < 0)
        return NULL;

    GtkWidget *stack = gtk_notebook_get_nth_page(omnibox->notebook, page);
    Tab *tab = g_object_get_data(G_OBJECT(stack), "tab");

    return tab != NULL
        ? g_strdup(webkit_web_view_get_uri(WEBKIT_WEB_VIEW(tab->view)))
        : NULL;
}

static GtkWidget *
icon_new(GdkPaintable *paintable, const char *fallback)
{
    GtkWidget *image = paintable != NULL
        ? gtk_image_new_from_paintable(paintable)
        : gtk_image_new_from_icon_name(fallback);

    gtk_image_set_pixel_size(GTK_IMAGE(image), 16);
    gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
    return image;
}

static GtkWidget *
row_new(GtkWidget *icon, const char *title, const char *subtitle)
{
    GtkWidget *row = gtk_button_new();
    gtk_widget_add_css_class(row, "flat");
    gtk_widget_set_focus_on_click(row, FALSE);
    gtk_widget_set_focusable(row, FALSE);
    gtk_widget_set_hexpand(row, TRUE);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);
    gtk_widget_set_margin_top(content, 6);
    gtk_widget_set_margin_bottom(content, 6);
    gtk_box_append(GTK_BOX(content), icon);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(text, TRUE);

    GtkWidget *title_label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(text), title_label);

    if (subtitle != NULL && *subtitle != '\0') {
        GtkWidget *subtitle_label = gtk_label_new(subtitle);
        gtk_widget_add_css_class(subtitle_label, "caption");
        gtk_widget_add_css_class(subtitle_label, "accent");
        gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(subtitle_label), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(text), subtitle_label);
    }

    gtk_box_append(GTK_BOX(content), text);
    gtk_button_set_child(GTK_BUTTON(row), content);
    return row;
}

static GtkWidget *
section_new(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_add_css_class(label, "caption");
    gtk_widget_add_css_class(label, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 8);
    gtk_widget_set_margin_bottom(label, 2);
    return label;
}

static void
on_tab_row_clicked(GtkButton *button, Omnibox *omnibox)
{
    GtkWidget *page = g_object_get_data(G_OBJECT(button), "page");

    if (page == NULL)
        return;

    omnibox_close(omnibox);
    omnibox->open_tab(page, omnibox->user_data);
}

static void
on_search_row_clicked(GtkButton *button, Omnibox *omnibox)
{
    const char *query = g_object_get_data(G_OBJECT(button), "query");

    if (query == NULL)
        return;

    omnibox_close(omnibox);
    omnibox->search(query, omnibox->user_data);
}

static int
append_tabs(Omnibox *omnibox, const char *needle)
{
    int pages = gtk_notebook_get_n_pages(omnibox->notebook);
    int shown = 0;

    for (int i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(omnibox->notebook, i);
        Tab *tab = g_object_get_data(G_OBJECT(page), "tab");

        if (tab == NULL)
            continue;

        WebKitWebView *web_view = WEBKIT_WEB_VIEW(tab->view);
        const char *uri = webkit_web_view_get_uri(web_view);
        const char *title = webkit_web_view_get_title(web_view);
        char *host = uri_host(uri);

        if (title == NULL || *title == '\0')
            title = "New Tab";

        if (*needle != '\0' &&
            !matches(needle, title) &&
            !matches(needle, host) &&
            !matches(needle, uri)) {
            g_free(host);
            continue;
        }

        GtkWidget *row = row_new(
            tab->icon != NULL
                ? icon_new(GDK_PAINTABLE(tab->icon), NULL)
                : icon_new(NULL, "web-browser-symbolic"),
            title,
            host
        );

        gtk_widget_set_tooltip_text(row, "Switch to Tab");
        g_object_set_data(G_OBJECT(row), "page", page);
        g_signal_connect(row, "clicked", G_CALLBACK(on_tab_row_clicked), omnibox);
        gtk_box_append(GTK_BOX(omnibox->list), row);

        g_free(host);
        shown++;
    }

    return shown;
}

static int
append_recent_searches(Omnibox *omnibox, const char *needle)
{
    GPtrArray *recent = g_ptr_array_new();

    for (int i = 0; i < search_history_count(omnibox->history); i++) {
        const char *query = search_history_get(omnibox->history, i);

        if (*needle == '\0' || matches(needle, query))
            g_ptr_array_add(recent, (gpointer)query);
    }

    if (recent->len > 0) {
        gtk_box_append(GTK_BOX(omnibox->list), section_new("Recent Searches"));

        for (guint i = 0; i < recent->len; i++) {
            const char *query = g_ptr_array_index(recent, i);
            GtkWidget *row = row_new(icon_new(NULL, "document-open-recent-symbolic"),
                                     query, NULL);

            g_object_set_data_full(G_OBJECT(row), "query", g_strdup(query), g_free);
            g_signal_connect(row, "clicked", G_CALLBACK(on_search_row_clicked), omnibox);
            gtk_box_append(GTK_BOX(omnibox->list), row);
        }
    }

    int shown = (int)recent->len;
    g_ptr_array_unref(recent);
    return shown;
}

static void
rebuild(Omnibox *omnibox)
{
    clear_box(omnibox->list);

    const char *text = gtk_editable_get_text(GTK_EDITABLE(omnibox->entry));
    char *uri = current_uri(omnibox);
    gboolean filtering = *text != '\0' && g_strcmp0(text, uri) != 0;
    g_free(uri);

    char *needle = filtering ? g_utf8_casefold(text, -1) : g_strdup("");
    int shown = append_tabs(omnibox, needle);
    shown += append_recent_searches(omnibox, needle);
    g_free(needle);

    if (shown == 0 && filtering) {
        GtkWidget *empty = gtk_label_new("No matches");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_widget_set_margin_top(empty, 12);
        gtk_widget_set_margin_bottom(empty, 12);
        gtk_box_append(GTK_BOX(omnibox->list), empty);
    }
}

static void
open_popover(Omnibox *omnibox)
{
    int width = gtk_widget_get_width(omnibox->entry);

    rebuild(omnibox);
    gtk_widget_set_size_request(omnibox->popover, width > 0 ? width : 420, -1);
    gtk_popover_popup(GTK_POPOVER(omnibox->popover));
    gtk_widget_grab_focus(omnibox->entry);
    omnibox->open = TRUE;
}

static void
on_popover_closed(GtkPopover *popover, Omnibox *omnibox)
{
    (void)popover;
    omnibox->open = FALSE;
}

static gboolean
clear_closing(gpointer data)
{
    Omnibox *omnibox = data;
    omnibox->closing = FALSE;
    omnibox->closing_id = 0;
    return G_SOURCE_REMOVE;
}

static void
on_entry_destroy(GtkWidget *entry, Omnibox *omnibox)
{
    (void)entry;

    omnibox->open = FALSE;

    if (omnibox->popover != NULL) {
        gtk_widget_unparent(omnibox->popover);
        omnibox->popover = NULL;
    }
}

static void
on_outside_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
                   Omnibox *omnibox)
{
    (void)n_press;

    if (!omnibox->open)
        return;

    GtkWidget *self = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GtkWidget *picked = gtk_widget_pick(self, x, y, GTK_PICK_DEFAULT);

    /* Clicks on the entry belong to the address bar; anything else dismisses. */
    if (picked == NULL ||
        picked == omnibox->entry ||
        gtk_widget_is_ancestor(picked, omnibox->entry))
        return;

    omnibox_close(omnibox);
}

static void
on_entry_changed(GtkEditable *editable, Omnibox *omnibox)
{
    (void)editable;

    if (omnibox->open)
        rebuild(omnibox);
}

static void
on_focus_enter(GtkEventControllerFocus *controller, Omnibox *omnibox)
{
    (void)controller;

    gtk_editable_select_region(GTK_EDITABLE(omnibox->entry), 0, -1);

    /* Closing a popover hands focus back to the entry; that focus event must
     * not immediately reopen the list. */
    if (!omnibox->open && !omnibox->closing)
        open_popover(omnibox);
}

static void
on_entry_clicked(GtkGestureClick *gesture, int n_press, double x, double y,
                 Omnibox *omnibox)
{
    (void)n_press;
    (void)x;
    (void)y;

    /* Let the entry place its caret and handle selection normally. */
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);

    gtk_widget_grab_focus(omnibox->entry);

    if (!omnibox->open)
        open_popover(omnibox);
}

static void
on_icon_press(GtkEntry *entry, GtkEntryIconPosition position, Omnibox *omnibox)
{
    (void)position;

    gtk_widget_grab_focus(GTK_WIDGET(entry));

    if (!omnibox->open)
        open_popover(omnibox);
}

static gboolean
on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
               GdkModifierType state, Omnibox *omnibox)
{
    (void)controller;
    (void)keycode;
    (void)state;

    if (keyval != GDK_KEY_Escape || !omnibox->open)
        return FALSE;

    omnibox_close(omnibox);
    return TRUE;
}

Omnibox *
omnibox_new(GtkWidget *entry,
            GtkNotebook *notebook,
            OmniboxOpenTabFunc open_tab,
            OmniboxSearchFunc search,
            gpointer user_data)
{
    Omnibox *omnibox = g_new0(Omnibox, 1);

    omnibox->entry = entry;
    omnibox->notebook = notebook;
    omnibox->open_tab = open_tab;
    omnibox->search = search;
    omnibox->user_data = user_data;
    omnibox->history = search_history_load();

    GtkWidget *scrolled = gtk_scrolled_window_new();
    /* The list must never take keyboard focus from the address entry. */
    gtk_widget_set_focusable(scrolled, FALSE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 380);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
    gtk_widget_set_size_request(scrolled, 420, -1);

    omnibox->list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(omnibox->list, 6);
    gtk_widget_set_margin_bottom(omnibox->list, 6);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), omnibox->list);

    omnibox->popover = gtk_popover_new();
    /* An autohide popover installs a GTK grab, and key events are then sent to
     * the grab instead of the entry's inner GtkText, so typing stops. Dismiss
     * on outside presses ourselves instead. */
    gtk_popover_set_autohide(GTK_POPOVER(omnibox->popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(omnibox->popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(omnibox->popover), GTK_POS_BOTTOM);
    gtk_widget_set_can_focus(omnibox->popover, FALSE);
    gtk_popover_set_child(GTK_POPOVER(omnibox->popover), scrolled);
    gtk_widget_set_parent(omnibox->popover, entry);
    gtk_widget_set_visible(omnibox->popover, FALSE);
    g_signal_connect(omnibox->popover, "closed", G_CALLBACK(on_popover_closed), omnibox);

    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY,
                                      "system-search-symbolic");
    gtk_entry_set_icon_activatable(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY, TRUE);
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY,
                                    "Open tabs and recent searches");

    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_focus_enter), omnibox);
    gtk_widget_add_controller(entry, focus);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), omnibox);
    gtk_widget_add_controller(entry, keys);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_entry_clicked), omnibox);
    gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(click));

    g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), omnibox);
    g_signal_connect(entry, "icon-press", G_CALLBACK(on_icon_press), omnibox);
    g_signal_connect(entry, "destroy", G_CALLBACK(on_entry_destroy), omnibox);

    GtkRoot *root = gtk_widget_get_root(entry);
    if (root != NULL) {
        GtkGesture *outside = gtk_gesture_click_new();

        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(outside),
                                                   GTK_PHASE_CAPTURE);
        g_signal_connect(outside, "pressed", G_CALLBACK(on_outside_pressed), omnibox);
        gtk_widget_add_controller(GTK_WIDGET(root), GTK_EVENT_CONTROLLER(outside));
    }

    return omnibox;
}

void
omnibox_close(Omnibox *omnibox)
{
    if (omnibox == NULL || omnibox->popover == NULL || !omnibox->open)
        return;

    omnibox->open = FALSE;
    omnibox->closing = TRUE;
    gtk_popover_popdown(GTK_POPOVER(omnibox->popover));

    if (omnibox->closing_id == 0)
        omnibox->closing_id = g_idle_add(clear_closing, omnibox);
}

void
omnibox_record_search(Omnibox *omnibox, const char *query)
{
    if (omnibox == NULL)
        return;

    search_history_add(omnibox->history, query);

    if (omnibox->open)
        rebuild(omnibox);
}

void
omnibox_free(Omnibox *omnibox)
{
    if (omnibox == NULL)
        return;

    if (omnibox->closing_id != 0)
        g_source_remove(omnibox->closing_id);

    search_history_free(omnibox->history);
    g_free(omnibox);
}
