#pragma once

#include <glib.h>

/* The two window chrome layouts Indigo knows how to build. */
typedef enum {
    INDIGO_LOOK_KDE = 0,
    INDIGO_LOOK_GNOME = 1,
} IndigoLook;

/* The look for this session. GNOME desktops get a header bar and Adwaita;
 * KDE and unknown desktops keep the toolbar layout and the system theme.
 * INDIGO_LOOK=gnome or INDIGO_LOOK=kde overrides detection. */
IndigoLook indigo_look_current(void);

/* Apply the styling that belongs with the look. Only the GNOME look changes
 * anything: it replaces leftover KDE/Breeze GTK styling with Adwaita. */
void indigo_look_apply_theme(IndigoLook look);
