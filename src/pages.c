#include "pages.h"

#include <cairo.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include "util.h"

#define MAX_TILES 15
#define TILE_ICON_SIZE 48
#define MONOGRAM_SIZE (TILE_ICON_SIZE * 2)
#define APP_ICON "com.projectindigo.Browser"

static const char *SETTINGS_CSS =
    "html, body { margin: 0; height: 100%; }"
    "body { display: flex; align-items: center; justify-content: center;"
    "       font-family: system-ui, sans-serif; font-size: 22px;"
    "       background: #fafafb; color: #242424; }"
    "@media (prefers-color-scheme: dark) {"
    "  body { background: #1d1d20; color: #deddda; }"
    "}";

static const struct {
    const char *url;
    const char *label;
} SUGGESTED[] = {
    { "https://en.wikipedia.org", "Wikipedia" },
    { "https://github.com", "GitHub" },
    { "https://news.ycombinator.com", "Hacker News" },
    { "https://www.youtube.com", "YouTube" },
    { "https://www.reddit.com", "Reddit" },
    { "https://duckduckgo.com", "DuckDuckGo" },
};

typedef struct {
    PagesOpenFunc open;
    gpointer user_data;
} TileAction;

gboolean
page_is_internal(const char *uri)
{
    return uri != NULL && g_str_has_prefix(uri, "indigo:");
}

static void
rounded_rectangle(cairo_t *cr, double width, double height, double radius)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI_2, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI_2);
    cairo_arc(cr, radius, height - radius, radius, G_PI_2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3 * G_PI_2);
    cairo_close_path(cr);
}

/* Cairo ARGB32 is premultiplied native-endian, which is GDK_MEMORY_DEFAULT. */
static GdkTexture *
texture_from_surface(cairo_surface_t *surface)
{
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    GBytes *bytes = g_bytes_new(cairo_image_surface_get_data(surface),
                                (gsize)stride * (gsize)height);
    GdkTexture *texture =
        gdk_memory_texture_new(width, height, GDK_MEMORY_DEFAULT, bytes, stride);

    g_bytes_unref(bytes);
    return texture;
}

/* A tile without a favicon still needs a stable, distinct colour. */
static GdkTexture *
monogram_texture(const char *host)
{
    char monogram[8] = "?";
    gunichar first = g_utf8_get_char_validated(host, -1);

    if (first != (gunichar)-1 && first != (gunichar)-2)
        monogram[g_unichar_to_utf8(g_unichar_toupper(first), monogram)] = '\0';

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, MONOGRAM_SIZE, MONOGRAM_SIZE);
    cairo_t *cr = cairo_create(surface);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_new();
    float red, green, blue;

    gtk_hsv_to_rgb((float)(g_str_hash(host) % 360) / 360.0f, 0.45f, 0.72f,
                   &red, &green, &blue);

    rounded_rectangle(cr, MONOGRAM_SIZE, MONOGRAM_SIZE, MONOGRAM_SIZE * 0.28);
    cairo_set_source_rgb(cr, red, green, blue);
    cairo_fill(cr);

    pango_font_description_set_weight(font, PANGO_WEIGHT_SEMIBOLD);
    pango_font_description_set_absolute_size(font, MONOGRAM_SIZE * 0.46 * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, monogram, -1);

    int width, height;
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, (MONOGRAM_SIZE - width) / 2.0, (MONOGRAM_SIZE - height) / 2.0);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_flush(surface);

    GdkTexture *texture = texture_from_surface(surface);
    cairo_surface_destroy(surface);

    return texture;
}

static void
on_tile_clicked(GtkButton *button, TileAction *action)
{
    const char *uri = g_object_get_data(G_OBJECT(button), "uri");

    if (uri != NULL)
        action->open(uri, action->user_data);
}

static GtkWidget *
tile_new(const char *url, const char *title, TileAction *action)
{
    char *host = uri_host(url);
    const char *label = (title != NULL && *title != '\0')
        ? title
        : (host != NULL ? host : url);
    GdkTexture *texture = monogram_texture(host != NULL ? host : label);

    GtkWidget *image = gtk_image_new_from_paintable(GDK_PAINTABLE(texture));
    gtk_image_set_pixel_size(GTK_IMAGE(image), TILE_ICON_SIZE);
    g_object_unref(texture);

    GtkWidget *text = gtk_label_new(label);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(text), 18);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), image);
    gtk_box_append(GTK_BOX(content), text);

    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "flat");
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_widget_set_tooltip_text(button, url);

    g_object_set_data_full(G_OBJECT(button), "uri", g_strdup(url), g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(on_tile_clicked), action);

    g_free(host);
    return button;
}

static GtkWidget *
brand_new(void)
{
    GtkWidget *logo = gtk_image_new_from_icon_name(APP_ICON);
    GtkWidget *name = gtk_label_new("Indigo");
    GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    gtk_image_set_pixel_size(GTK_IMAGE(logo), 44);
    gtk_widget_add_css_class(name, "title-1");
    gtk_widget_set_halign(brand, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(brand), logo);
    gtk_box_append(GTK_BOX(brand), name);

    return brand;
}

static GtkWidget *
grid_new(History *history, TileAction *action)
{
    GPtrArray *sites = history_top_sites(history, MAX_TILES);
    GtkWidget *grid = gtk_flow_box_new();

    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(grid), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid), TRUE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid), 8);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(grid, GTK_ALIGN_START);

    if (sites->len == 0) {
        for (guint i = 0; i < G_N_ELEMENTS(SUGGESTED); i++) {
            gtk_flow_box_append(GTK_FLOW_BOX(grid),
                                tile_new(SUGGESTED[i].url, SUGGESTED[i].label, action));
        }
    } else {
        for (guint i = 0; i < sites->len; i++) {
            const HistoryEntry *entry = g_ptr_array_index(sites, i);

            gtk_flow_box_append(GTK_FLOW_BOX(grid),
                                tile_new(entry->url, entry->title, action));
        }
    }

    g_ptr_array_unref(sites);
    return grid;
}

GtkWidget *
pages_new_tab_new(History *history, PagesOpenFunc open, gpointer user_data)
{
    gboolean suggested = history_count(history) == 0;
    TileAction *action = g_new0(TileAction, 1);

    action->open = open;
    action->user_data = user_data;

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    /* A new tab starts with the page focused, not the address bar. */
    gtk_widget_set_focusable(scrolled, TRUE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 56);
    gtk_widget_set_margin_bottom(box, 40);

    gtk_box_append(GTK_BOX(box), brand_new());

    if (suggested) {
        GtkWidget *caption = gtk_label_new("Suggested");

        gtk_widget_add_css_class(caption, "caption");
        gtk_widget_add_css_class(caption, "dim-label");
        gtk_widget_set_margin_top(caption, 36);
        gtk_widget_set_margin_bottom(caption, 12);
        gtk_box_append(GTK_BOX(box), caption);
    }

    GtkWidget *grid = grid_new(history, action);
    gtk_widget_set_margin_top(grid, suggested ? 0 : 40);
    gtk_box_append(GTK_BOX(box), grid);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    g_object_set_data_full(G_OBJECT(scrolled), "tile-action", action, g_free);

    return scrolled;
}

char *
pages_settings_html(void)
{
    return g_strdup_printf(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Settings</title>"
        "<style>%s</style></head><body>settings</body></html>",
        SETTINGS_CSS);
}
