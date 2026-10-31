#include "look.h"

#include <gtk/gtk.h>

/* Desktop names are colon separated, e.g. "ubuntu:GNOME". */
static gboolean
look_from_desktop(const char *desktop, IndigoLook *look)
{
    if (desktop == NULL)
        return FALSE;

    char **parts = g_strsplit(desktop, ":", -1);
    gboolean found = FALSE;

    for (guint i = 0; parts[i] != NULL && !found; i++) {
        const char *name = g_strstrip(parts[i]);

        if (g_ascii_strcasecmp(name, "GNOME") == 0) {
            *look = INDIGO_LOOK_GNOME;
            found = TRUE;
        } else if (g_ascii_strcasecmp(name, "KDE") == 0) {
            *look = INDIGO_LOOK_KDE;
            found = TRUE;
        }
    }

    g_strfreev(parts);
    return found;
}

IndigoLook
indigo_look_current(void)
{
    static const char *const variables[] = {
        "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_DESKTOP",
        "DESKTOP_SESSION",
    };
    const char *override = g_getenv("INDIGO_LOOK");

    if (override != NULL) {
        if (g_ascii_strcasecmp(override, "gnome") == 0)
            return INDIGO_LOOK_GNOME;
        if (g_ascii_strcasecmp(override, "kde") == 0)
            return INDIGO_LOOK_KDE;
    }

    for (guint i = 0; i < G_N_ELEMENTS(variables); i++) {
        IndigoLook look;

        if (look_from_desktop(g_getenv(variables[i]), &look))
            return look;
    }

    return INDIGO_LOOK_KDE;
}

/* GNOME's color-scheme setting selects the dark Adwaita variant. */
static gboolean
prefers_dark(void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = NULL;
    gboolean dark = FALSE;

    if (source != NULL) {
        schema = g_settings_schema_source_lookup(source,
                                                 "org.gnome.desktop.interface",
                                                 TRUE);
    }

    if (schema != NULL) {
        GSettings *settings = g_settings_new_full(schema, NULL, NULL);
        char *scheme = g_settings_get_string(settings, "color-scheme");

        dark = g_strcmp0(scheme, "prefer-dark") == 0;
        g_free(scheme);
        g_object_unref(settings);
        g_settings_schema_unref(schema);
    } else {
        GtkSettings *settings = gtk_settings_get_default();

        if (settings != NULL) {
            g_object_get(settings, "gtk-application-prefer-dark-theme",
                         &dark, NULL);
        }
    }

    return dark;
}

void
indigo_look_apply_theme(IndigoLook look)
{
    GtkSettings *settings;
    char *theme = NULL;

    if (look != INDIGO_LOOK_GNOME)
        return;

    settings = gtk_settings_get_default();
    if (settings == NULL)
        return;

    g_object_get(settings, "gtk-theme-name", &theme, NULL);

    /* A KDE session leaves Breeze, KDE's icons, and its window-button layout
     * behind; that is what makes Indigo look like a KDE application on GNOME.
     * Replace those when the GNOME look is active. */
    if (theme == NULL || g_ascii_strncasecmp(theme, "Breeze", 6) != 0) {
        g_free(theme);
        return;
    }

    g_object_set(settings,
                 "gtk-theme-name", "Adwaita",
                 "gtk-icon-theme-name", "Adwaita",
                 "gtk-decoration-layout", "appmenu:close",
                 "gtk-application-prefer-dark-theme", prefers_dark(),
                 NULL);
    g_free(theme);
}
