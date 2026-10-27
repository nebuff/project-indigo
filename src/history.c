#include "history.h"

#include <string.h>

#include <glib/gstdio.h>

#define MAX_ENTRIES 500

struct _History {
    GPtrArray *entries;
};

static void
entry_free(gpointer data)
{
    HistoryEntry *entry = data;

    g_free(entry->url);
    g_free(entry->title);
    g_free(entry);
}

static char *
history_path(void)
{
    return g_build_filename(g_get_user_state_dir(), "indigo", "history", NULL);
}

/* The store is tab separated, one entry per line. */
static char *
single_line(const char *text)
{
    char *copy = g_strdup(text != NULL ? text : "");

    for (char *p = copy; *p != '\0'; p++) {
        if (*p == '\t' || *p == '\r' || *p == '\n')
            *p = ' ';
    }

    return copy;
}

static void
save(const History *history)
{
    GString *contents = g_string_new(NULL);

    for (guint i = 0; i < history->entries->len; i++) {
        const HistoryEntry *entry = g_ptr_array_index(history->entries, i);
        char *title = single_line(entry->title);

        g_string_append_printf(contents, "%" G_GINT64_FORMAT "\t%u\t%s\t%s\n",
                               entry->last_visit, entry->visits, entry->url, title);
        g_free(title);
    }

    char *path = history_path();
    char *directory = g_path_get_dirname(path);

    g_mkdir_with_parents(directory, 0700);
    g_file_set_contents(path, contents->str, (gssize)contents->len, NULL);

    g_free(directory);
    g_free(path);
    g_string_free(contents, TRUE);
}

History *
history_load(void)
{
    History *history = g_new0(History, 1);
    char *path = history_path();
    char *contents = NULL;

    history->entries = g_ptr_array_new_with_free_func(entry_free);

    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        char **lines = g_strsplit(contents, "\n", -1);

        for (char **line = lines; *line != NULL && history->entries->len < MAX_ENTRIES;
             line++) {
            char **fields = g_strsplit(*line, "\t", 4);

            if (fields[0] != NULL && fields[1] != NULL &&
                fields[2] != NULL && *fields[2] != '\0') {
                HistoryEntry *entry = g_new0(HistoryEntry, 1);

                entry->last_visit = g_ascii_strtoll(fields[0], NULL, 10);
                entry->visits = (guint)g_ascii_strtoull(fields[1], NULL, 10);
                entry->url = g_strdup(fields[2]);
                entry->title = g_strdup(fields[3] != NULL ? fields[3] : "");

                g_ptr_array_add(history->entries, entry);
            }

            g_strfreev(fields);
        }

        g_strfreev(lines);
        g_free(contents);
    }

    g_free(path);
    return history;
}

void
history_free(History *history)
{
    if (history == NULL)
        return;

    g_ptr_array_unref(history->entries);
    g_free(history);
}

static guint
find_index(const History *history, const char *url)
{
    for (guint i = 0; i < history->entries->len; i++) {
        const HistoryEntry *entry = g_ptr_array_index(history->entries, i);

        if (strcmp(entry->url, url) == 0)
            return i;
    }

    return G_MAXUINT;
}

void
history_record(History *history, const char *url, const char *title)
{
    /* Internal pages and blank documents are not browsing history. */
    if (history == NULL || url == NULL ||
        (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")))
        return;

    guint index = find_index(history, url);
    HistoryEntry *entry;

    if (index == G_MAXUINT) {
        entry = g_new0(HistoryEntry, 1);
        entry->url = g_strdup(url);
        /* Entries always have a title, even when the page has none. */
        entry->title = g_strdup("");
    } else {
        /* Taking the entry out moves it to the front without freeing it. */
        entry = g_ptr_array_steal_index(history->entries, index);
    }

    entry->visits++;
    entry->last_visit = g_get_real_time();

    if (title != NULL && *title != '\0') {
        g_free(entry->title);
        entry->title = g_strdup(title);
    }

    g_ptr_array_insert(history->entries, 0, entry);

    if (history->entries->len > MAX_ENTRIES)
        g_ptr_array_remove_index(history->entries, history->entries->len - 1);

    save(history);
}

void
history_clear(History *history)
{
    if (history == NULL)
        return;

    g_ptr_array_set_size(history->entries, 0);
    save(history);
}

int
history_count(const History *history)
{
    return history != NULL ? (int)history->entries->len : 0;
}

const HistoryEntry *
history_get(const History *history, int index)
{
    if (history == NULL || index < 0 || index >= (int)history->entries->len)
        return NULL;

    return g_ptr_array_index(history->entries, (guint)index);
}

static int
compare_visits(gconstpointer a, gconstpointer b)
{
    const HistoryEntry *left = *(const HistoryEntry *const *)a;
    const HistoryEntry *right = *(const HistoryEntry *const *)b;

    if (left->visits != right->visits)
        return left->visits < right->visits ? 1 : -1;

    return left->last_visit < right->last_visit ? 1 : -1;
}

GPtrArray *
history_top_sites(const History *history, guint max)
{
    GPtrArray *sites = g_ptr_array_new();

    if (history == NULL)
        return sites;

    for (guint i = 0; i < history->entries->len; i++)
        g_ptr_array_add(sites, g_ptr_array_index(history->entries, i));

    g_ptr_array_sort(sites, compare_visits);
    g_ptr_array_set_size(sites, MIN(sites->len, max));

    return sites;
}
