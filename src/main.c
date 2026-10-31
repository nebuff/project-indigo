#include <gtk/gtk.h>
#include "browser.h"

static const char *application_id(void) {
    return "com.projectindigo.Browser";
}

void
browser_activate(GtkApplication *app, gpointer G_GNUC_UNUSED user_data)
{
    GtkWidget *window = browser_window_new(app);
    Browser *browser = g_object_get_data(G_OBJECT(window), "browser");

    g_return_if_fail(browser != NULL);
    browser_show(browser);
}

int
main(int argc, char **argv)
{
    const char *non_unique = g_getenv("INDIGO_NON_UNIQUE");
    GApplicationFlags flags = G_APPLICATION_DEFAULT_FLAGS;

    /* A development build must open its own window instead of activating an
     * installed Indigo over D-Bus. */
    if (non_unique != NULL && *non_unique != '\0')
        flags |= G_APPLICATION_NON_UNIQUE;

    GtkApplication *app = gtk_application_new(application_id(), flags);
    g_signal_connect(app, "activate", G_CALLBACK(browser_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
