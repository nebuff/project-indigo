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
    GtkApplication *app = gtk_application_new(application_id(),
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(browser_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
