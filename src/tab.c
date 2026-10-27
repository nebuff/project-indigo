#include "tab.h"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

static void
on_title_changed(WebKitWebView *view, GParamSpec *pspec, Tab *tab)
{
    (void)pspec;

    /* A page behind the new tab page must not rename the tab. */
    if (!tab_showing_web(tab))
        return;

    const char *title = webkit_web_view_get_title(view);

    gtk_label_set_text(
        GTK_LABEL(tab->label),
        (title != NULL && *title != '\0') ? title : "New Tab"
    );
}

static void
on_favicon_changed(WebKitWebView *view, GParamSpec *pspec, Tab *tab)
{
    (void)pspec;

    g_clear_object(&tab->icon);

    GdkTexture *texture = webkit_web_view_get_favicon(view);
    if (texture != NULL)
        tab->icon = g_object_ref(texture);
}

Tab *
tab_new(GtkWidget *new_tab_page)
{
    Tab *tab = g_new0(Tab, 1);

    tab->view = webkit_web_view_new();
    tab->label = gtk_label_new("New Tab");
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    /* An ellipsized label requests almost no width, so the tab would collapse
     * to "..."; give it a floor and the full title up to a readable cap. */
    gtk_label_set_width_chars(GTK_LABEL(tab->label), 20);
    gtk_label_set_max_width_chars(GTK_LABEL(tab->label), 40);

    tab->stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(tab->stack), new_tab_page, "newtab");
    gtk_stack_add_named(GTK_STACK(tab->stack), tab->view, "web");

    g_signal_connect(tab->view, "notify::title", G_CALLBACK(on_title_changed), tab);
    g_signal_connect(tab->view, "notify::favicon", G_CALLBACK(on_favicon_changed), tab);

    return tab;
}

gboolean
tab_showing_web(Tab *tab)
{
    return tab != NULL &&
        gtk_stack_get_visible_child(GTK_STACK(tab->stack)) == tab->view;
}

void
tab_show_web(Tab *tab)
{
    gtk_stack_set_visible_child_name(GTK_STACK(tab->stack), "web");
}

void
tab_free(Tab *tab)
{
    if (tab == NULL)
        return;

    g_clear_object(&tab->icon);
    g_free(tab);
}
