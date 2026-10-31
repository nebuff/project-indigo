#include "browser.h"

#include "history.h"
#include "look.h"
#include "menu.h"
#include "omnibox.h"
#include "pages.h"
#include "tab.h"

#include <string.h>

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#define SEARCH_URL "https://www.google.com/search?q="

struct _Browser {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *back;
    GtkWidget *forward;
    GtkWidget *refresh;
    GtkWidget *uri;
    History *history;
    Omnibox *omnibox;
    Menu *menu;
    guint closing_idle;
    GtkWidget *closing_window;
};

/* ---- internal helpers ---- */

static void browser_free_gtk(gpointer data);
static void show_start_page(Browser *browser, Tab *tab);
static void show_web_page(Browser *browser, Tab *tab);
static Tab *add_tab(Browser *browser, const char *uri);

static void
browser_free_gtk(gpointer data)
{
    Browser *browser = data;

    omnibox_free(browser->omnibox);
    menu_free(browser->menu);
    history_free(browser->history);
    if (browser->closing_idle != 0) {
        g_source_remove(browser->closing_idle);
        browser->closing_idle = 0;
    }
    g_free(browser);

    return;
}

static Tab *
tab_for_page(GtkWidget *page)
{
    return g_object_get_data(G_OBJECT(page), "tab");
}

static Tab *
current_tab(Browser *browser)
{
    GtkNotebook *notebook = GTK_NOTEBOOK(browser->notebook);
    int page = gtk_notebook_get_current_page(notebook);

    if (page < 0)
        return NULL;

    return tab_for_page(gtk_notebook_get_nth_page(notebook, page));
}

static gboolean
entry_is_focused(GtkWidget *entry)
{
    GtkRoot *root = gtk_widget_get_root(entry);
    GtkWidget *focus = root != NULL ? gtk_root_get_focus(root) : NULL;

    return focus != NULL && (focus == entry || gtk_widget_is_ancestor(focus, entry));
}

static void
update_chrome_for_tab(Browser *browser, Tab *tab, gboolean sync_uri)
{
    if (tab == NULL) {
        gtk_widget_set_sensitive(browser->back, FALSE);
        gtk_widget_set_sensitive(browser->forward, FALSE);
        gtk_widget_set_sensitive(browser->refresh, FALSE);
        if (sync_uri)
            gtk_editable_set_text(GTK_EDITABLE(browser->uri), "");
        return;
    }

    WebKitWebView *view = WEBKIT_WEB_VIEW(tab->view);
    gboolean showing_web = tab_showing_web(tab);

    gtk_widget_set_sensitive(browser->back,
                             webkit_web_view_can_go_back(view) ||
                                 (showing_web && tab->start_in_history));
    gtk_widget_set_sensitive(browser->forward,
                             showing_web
                                 ? webkit_web_view_can_go_forward(view)
                                 : tab->start_in_history);
    gtk_widget_set_sensitive(browser->refresh, showing_web);

    if (!sync_uri && entry_is_focused(browser->uri))
        return;

    const char *uri = showing_web ? webkit_web_view_get_uri(view) : NULL;
    gtk_editable_set_text(GTK_EDITABLE(browser->uri),
                          uri != NULL && !page_is_internal(uri) ? uri : "");
}

static void
update_chrome(Browser *browser, gboolean sync_uri)
{
    update_chrome_for_tab(browser, current_tab(browser), sync_uri);
}

static void
on_view_changed(GObject *object, GParamSpec *pspec, Browser *browser)
{
    (void)pspec;

    Tab *tab = current_tab(browser);
    if (tab != NULL && tab->view == GTK_WIDGET(object))
        update_chrome(browser, FALSE);
}

static void
on_load_changed(WebKitWebView *view, WebKitLoadEvent event, Browser *browser)
{
    if (event != WEBKIT_LOAD_COMMITTED && event != WEBKIT_LOAD_FINISHED)
        return;

    Tab *tab = current_tab(browser);
    if (tab != NULL && tab->view == GTK_WIDGET(view))
        update_chrome(browser, FALSE);

    if (event == WEBKIT_LOAD_FINISHED)
        history_record(browser->history, webkit_web_view_get_uri(view),
                       webkit_web_view_get_title(view));
}

#ifdef WEBKIT_HAVE_FEATURE_WEB_PROCESS_CRASHED
static void
on_web_process_crashed(WebKitWebView *view,
                        WebKitWebProcessFailedState state,
                        const gchar *error_message,
                        gpointer user_data)
{
    (void)error_message;

    if (state != WEBKIT_WEB_PROCESS_FAILED_CRASHED)
        return;

    Browser *browser = user_data;
    Tab *tab = current_tab(browser);

    if (tab != NULL && gtk_widget_get_visible(GTK_WIDGET(view)))
        webkit_web_view_reload(WEBKIT_WEB_VIEW(tab->view));
}
#endif

static void
on_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num,
                Browser *browser)
{
    (void)notebook;
    (void)page_num;

    update_chrome_for_tab(browser, tab_for_page(page), TRUE);
}

static void
on_back_clicked(GtkButton *button, Browser *browser)
{
    (void)button;
    Tab *tab = current_tab(browser);

    if (tab == NULL)
        return;

    if (webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(tab->view)))
        webkit_web_view_go_back(WEBKIT_WEB_VIEW(tab->view));
    else if (tab_showing_web(tab) && tab->start_in_history)
        show_start_page(browser, tab);
}

static void
on_forward_clicked(GtkButton *button, Browser *browser)
{
    (void)button;
    Tab *tab = current_tab(browser);

    if (tab == NULL)
        return;

    if (!tab_showing_web(tab))
        show_web_page(browser, tab);
    else if (webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(tab->view)))
        webkit_web_view_go_forward(WEBKIT_WEB_VIEW(tab->view));
}

static void
on_refresh_clicked(GtkButton *button, Browser *browser)
{
    (void)button;
    Tab *tab = current_tab(browser);

    if (tab != NULL)
        webkit_web_view_reload(WEBKIT_WEB_VIEW(tab->view));
}

static void
navigate(Browser *browser, const char *uri)
{
    Tab *tab = current_tab(browser);

    if (tab == NULL || uri == NULL)
        return;

    if (!tab_showing_web(tab))
        tab->start_in_history = TRUE;

    tab_show_web(tab);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->view), uri);
    gtk_widget_grab_focus(tab->view);
}

static void
show_start_page(Browser *browser, Tab *tab)
{
    gtk_stack_set_visible_child_name(GTK_STACK(tab->stack), "newtab");
    gtk_label_set_text(GTK_LABEL(tab->label), "New Tab");
    update_chrome(browser, TRUE);

    GtkWidget *page = gtk_stack_get_visible_child(GTK_STACK(tab->stack));
    if (page != NULL)
        gtk_widget_grab_focus(page);
}

static void
show_web_page(Browser *browser, Tab *tab)
{
    const char *title =
        webkit_web_view_get_title(WEBKIT_WEB_VIEW(tab->view));

    tab_show_web(tab);
    gtk_label_set_text(GTK_LABEL(tab->label),
                       title != NULL && *title != '\0' ? title : "New Tab");
    update_chrome(browser, TRUE);
    gtk_widget_grab_focus(tab->view);
}

static char *
search_uri(const char *query)
{
    char *escaped = g_uri_escape_string(query, NULL, FALSE);
    char *uri = g_strconcat(SEARCH_URL, escaped, NULL);
    g_free(escaped);
    return uri;
}

static gboolean
has_scheme(const char *text)
{
    return strstr(text, "://") != NULL ||
        g_str_has_prefix(text, "about:") ||
        g_str_has_prefix(text, "data:") ||
        g_str_has_prefix(text, "file:") ||
        g_str_has_prefix(text, "view-source:");
}

static gboolean
looks_like_host(const char *text)
{
    if (strpbrk(text, " \t") != NULL)
        return FALSE;

    size_t length = strcspn(text, "/?#:");
    if (length == 0)
        return FALSE;

    char *host = g_strndup(text, length);
    gboolean is_host = strchr(host, '.') != NULL ||
        g_ascii_strcasecmp(host, "localhost") == 0 ||
        g_str_has_prefix(host, "127.");
    g_free(host);
    return is_host;
}

static char *
resolve_input(const char *text, gboolean *searched)
{
    char *trimmed = g_strstrip(g_strdup(text));

    *searched = FALSE;

    if (*trimmed == '\0') {
        g_free(trimmed);
        return NULL;
    }

    char *uri;

    if (has_scheme(trimmed)) {
        uri = g_strdup(trimmed);
    } else if (looks_like_host(trimmed)) {
        uri = g_strconcat("https://", trimmed, NULL);
    } else {
        uri = search_uri(trimmed);
        *searched = TRUE;
    }

    g_free(trimmed);
    return uri;
}

static void
on_uri_activate(GtkEntry *entry, Browser *browser)
{
    gboolean searched = FALSE;
    char *uri = resolve_input(gtk_editable_get_text(GTK_EDITABLE(entry)),
                              &searched);

    if (uri == NULL)
        return;

    if (searched)
        omnibox_record_search(browser->omnibox,
                              gtk_editable_get_text(GTK_EDITABLE(entry)));

    omnibox_close(browser->omnibox);
    navigate(browser, uri);
    g_free(uri);
}

static void
on_new_tab_clicked(GtkButton *button, Browser *browser)
{
    (void)button;

    add_tab(browser, NULL);
    gtk_widget_grab_focus(browser->uri);
}

static void
on_close_clicked(GtkButton *button, Tab *tab)
{
    (void)button;

    Browser *browser = g_object_get_data(G_OBJECT(tab->view), "browser");
    GtkNotebook *notebook = GTK_NOTEBOOK(browser->notebook);

    if (gtk_notebook_get_n_pages(notebook) <= 1) {
        gtk_window_close(GTK_WINDOW(browser->window));
        return;
    }

    int page = gtk_notebook_page_num(notebook, tab->stack);
    if (page < 0)
        return;

    WebKitWebView *view = WEBKIT_WEB_VIEW(tab->view);
    menu_record_closed(browser->menu, webkit_web_view_get_uri(view),
                       webkit_web_view_get_title(view));

    gtk_notebook_remove_page(notebook, page);
}

static void
on_omnibox_open_tab(GtkWidget *page, gpointer user_data)
{
    Browser *browser = user_data;
    GtkNotebook *notebook = GTK_NOTEBOOK(browser->notebook);
    Tab *tab = tab_for_page(page);
    int index = gtk_notebook_page_num(notebook, page);

    if (tab == NULL || index < 0)
        return;

    gtk_notebook_set_current_page(notebook, index);

    if (tab_showing_web(tab))
        gtk_widget_grab_focus(tab->view);
    else
        gtk_widget_grab_focus(browser->uri);
}

static void
on_omnibox_search(const char *query, gpointer user_data)
{
    Browser *browser = user_data;

    omnibox_record_search(browser->omnibox, query);

    char *uri = search_uri(query);
    navigate(browser, uri);
    g_free(uri);
}

static void
on_open_uri(const char *uri, gpointer user_data)
{
    navigate(user_data, uri);
}

static void
on_menu_open_tab(const char *uri, gpointer user_data)
{
    Browser *browser = user_data;
    Tab *tab = add_tab(browser, uri);

    if (tab != NULL)
        gtk_widget_grab_focus(tab->view);
}

static void
on_menu_settings(gpointer user_data)
{
    Browser *browser = user_data;
    Tab *tab = current_tab(browser);

    if (tab == NULL)
        return;

    char *html = pages_settings_html();

    tab_show_web(tab);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(tab->view), html, PAGE_SETTINGS);
    g_free(html);
}

static gboolean
on_close_idle(gpointer user_data)
{
    Browser *browser = user_data;

    browser->closing_idle = 0;
    browser->closing_window = NULL;

    omnibox_free(browser->omnibox);
    menu_free(browser->menu);
    history_free(browser->history);
    g_free(browser);

    return G_SOURCE_REMOVE;
}

static void
on_destroy(GtkWidget *window, Browser *browser)
{
    if (browser == NULL)
        return;

    browser->closing_window = NULL;
    gtk_widget_set_visible(window, FALSE);

    if (browser->closing_idle == 0)
        browser->closing_idle =
            g_idle_add(on_close_idle, browser);
}

/* ---- tab helpers ---- */

static Tab *
add_tab(Browser *browser, const char *uri)
{
    GtkWidget *new_tab_page =
        pages_new_tab_new(browser->history, on_open_uri, browser);
    Tab *tab = tab_new(new_tab_page);
    GtkWidget *close =
        gtk_button_new_from_icon_name("window-close-symbolic");
    GtkWidget *label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkNotebook *notebook = GTK_NOTEBOOK(browser->notebook);

    gtk_widget_add_css_class(close, "flat");
    gtk_widget_set_tooltip_text(close, "Close Tab");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), tab);

    gtk_box_append(GTK_BOX(label), tab->label);
    gtk_box_append(GTK_BOX(label), close);

    int page = gtk_notebook_append_page(notebook, tab->stack, label);

    g_object_set_data_full(G_OBJECT(tab->stack), "tab", tab,
                           (GDestroyNotify)tab_free);
    g_object_set_data(G_OBJECT(tab->view), "browser", browser);
    g_signal_connect(tab->view, "notify::uri", G_CALLBACK(on_view_changed),
                     browser);
    g_signal_connect(tab->view, "load-changed", G_CALLBACK(on_load_changed),
                     browser);

#ifdef WEBKIT_HAVE_FEATURE_WEB_PROCESS_CRASHED
    g_signal_connect(tab->view, "web-process-crashed",
                     G_CALLBACK(on_web_process_crashed), browser);
#endif

    gtk_notebook_set_current_page(notebook, page);

    if (uri != NULL) {
        tab_show_web(tab);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(tab->view), uri);
    }

    return tab;
}

/* ---- constructor ---- */

GtkWidget *
browser_window_new(GtkApplication *application)
{
    Browser *browser = g_new0(Browser, 1);
    IndigoLook look = indigo_look_current();

    indigo_look_apply_theme(look);

    browser->history = history_load();
    browser->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(browser->window), "Indigo");
    gtk_window_set_default_size(GTK_WINDOW(browser->window), 1100, 760);

    browser->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(browser->notebook), TRUE);
    gtk_widget_set_vexpand(browser->notebook, TRUE);
    g_signal_connect(browser->notebook, "switch-page", G_CALLBACK(on_switch_page),
                     browser);

    browser->back = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_add_css_class(browser->back, "flat");
    gtk_widget_set_tooltip_text(browser->back, "Back");
    g_signal_connect(browser->back, "clicked", G_CALLBACK(on_back_clicked),
                     browser);

    browser->forward = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(browser->forward, "flat");
    gtk_widget_set_tooltip_text(browser->forward, "Forward");
    g_signal_connect(browser->forward, "clicked", G_CALLBACK(on_forward_clicked),
                     browser);

    browser->refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_add_css_class(browser->refresh, "flat");
    gtk_widget_set_tooltip_text(browser->refresh, "Reload page");
    g_signal_connect(browser->refresh, "clicked", G_CALLBACK(on_refresh_clicked),
                     browser);

    browser->uri = gtk_entry_new();
    gtk_widget_set_hexpand(browser->uri, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->uri),
                                   "Search or enter address");
    g_signal_connect(browser->uri, "activate", G_CALLBACK(on_uri_activate),
                     browser);

    GtkWidget *menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button),
                                   "open-menu-symbolic");
    gtk_widget_add_css_class(menu_button, "flat");
    gtk_widget_set_tooltip_text(menu_button, "Main Menu");

    GtkWidget *new_tab = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(new_tab, "flat");
    gtk_widget_set_tooltip_text(new_tab, "New Tab");
    g_signal_connect(new_tab, "clicked", G_CALLBACK(on_new_tab_clicked),
                     browser);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(browser->notebook), new_tab,
                                   GTK_PACK_END);

    if (look == INDIGO_LOOK_GNOME) {
        /* GNOME puts navigation and the location bar in the header bar. */
        GtkWidget *header = gtk_header_bar_new();

        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->back);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->forward);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), browser->refresh);
        gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), browser->uri);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);

        gtk_window_set_titlebar(GTK_WINDOW(browser->window), header);
        gtk_window_set_child(GTK_WINDOW(browser->window), browser->notebook);
    } else {
        /* KDE keeps the toolbar above the tab strip, like Dolphin. */
        GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        gtk_widget_set_margin_start(toolbar, 6);
        gtk_widget_set_margin_end(toolbar, 6);
        gtk_widget_set_margin_top(toolbar, 6);
        gtk_widget_set_margin_bottom(toolbar, 6);

        gtk_box_append(GTK_BOX(toolbar), browser->back);
        gtk_box_append(GTK_BOX(toolbar), browser->forward);
        gtk_box_append(GTK_BOX(toolbar), browser->refresh);
        gtk_box_append(GTK_BOX(toolbar), browser->uri);
        gtk_box_append(GTK_BOX(toolbar), menu_button);

        gtk_box_append(GTK_BOX(content), toolbar);
        gtk_box_append(GTK_BOX(content), browser->notebook);
        gtk_window_set_child(GTK_WINDOW(browser->window), content);
    }

    browser->omnibox = omnibox_new(browser->uri, GTK_NOTEBOOK(browser->notebook),
                                   on_omnibox_open_tab, on_omnibox_search,
                                   browser);

    browser->menu = menu_new(menu_button, browser->history, on_open_uri,
                             on_menu_open_tab, on_menu_settings, browser);

    g_object_set_data_full(G_OBJECT(browser->window), "browser", browser,
                           browser_free_gtk);

    browser->closing_window = browser->window;
    g_signal_connect(browser->window, "destroy", G_CALLBACK(on_destroy),
                     browser);

    Tab *first = add_tab(browser, NULL);

    if (first != NULL) {
        GtkWidget *page = gtk_stack_get_visible_child(GTK_STACK(first->stack));

        if (page != NULL)
            gtk_widget_grab_focus(page);
    }

    update_chrome(browser, TRUE);

    return browser->window;
}

void
browser_show(Browser *browser)
{
    g_return_if_fail(browser != NULL);
    gtk_widget_set_visible(browser->window, TRUE);
}

