#include "search_history.h"

#include <glib.h>
#include <glib/gstdio.h>

#define MAX_ENTRIES 8

struct _SearchHistory {
    GPtrArray *items;
};

static char *
history_path(void)
{
    return g_build_filename(g_get_user_state_dir(), "indigo", "search-history", NULL);
}

static void
save(const SearchHistory *history)
{
    GString *contents = g_string_new(NULL);

    for (guint i = 0; i < history->items->len; i++)
        g_string_append_printf(contents, "%s\n",
                               (const char *)g_ptr_array_index(history->items, i));

    char *path = history_path();
    char *directory = g_path_get_dirname(path);
    g_mkdir_with_parents(directory, 0700);
    g_file_set_contents(path, contents->str, (gssize)contents->len, NULL);

    g_free(directory);
    g_free(path);
    g_string_free(contents, TRUE);
}

SearchHistory *
search_history_load(void)
{
    SearchHistory *history = g_new0(SearchHistory, 1);
    history->items = g_ptr_array_new_with_free_func(g_free);

    char *path = history_path();
    char *contents = NULL;

    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        char **lines = g_strsplit(contents, "\n", -1);

        for (char **line = lines; *line != NULL && history->items->len < MAX_ENTRIES; line++) {
            char *query = g_strstrip(g_strdup(*line));

            if (*query == '\0') {
                g_free(query);
                continue;
            }

            g_ptr_array_add(history->items, query);
        }

        g_strfreev(lines);
        g_free(contents);
    }

    g_free(path);
    return history;
}

void
search_history_free(SearchHistory *history)
{
    if (history == NULL)
        return;

    g_ptr_array_unref(history->items);
    g_free(history);
}

void
search_history_add(SearchHistory *history, const char *query)
{
    if (history == NULL || query == NULL)
        return;

    char *trimmed = g_strstrip(g_strdup(query));
    if (*trimmed == '\0') {
        g_free(trimmed);
        return;
    }

    for (guint i = 0; i < history->items->len; i++) {
        if (g_ascii_strcasecmp(g_ptr_array_index(history->items, i), trimmed) == 0) {
            g_free(trimmed);
            return;
        }
    }

    g_ptr_array_insert(history->items, 0, trimmed);

    if (history->items->len > MAX_ENTRIES)
        g_ptr_array_remove_index(history->items, history->items->len - 1);

    save(history);
}

int
search_history_count(const SearchHistory *history)
{
    return history != NULL ? (int)history->items->len : 0;
}

const char *
search_history_get(const SearchHistory *history, int index)
{
    if (history == NULL || index < 0 || index >= (int)history->items->len)
        return NULL;

    return g_ptr_array_index(history->items, index);
}
