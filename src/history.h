#pragma once

#include <glib.h>

typedef struct {
    char *url;
    char *title;
    guint visits;
    gint64 last_visit;
} HistoryEntry;

typedef struct _History History;

/* Browsing history: one entry per visited URL, most recently visited first,
 * persisted in the user's state directory. */
History *history_load(void);
void history_free(History *history);
void history_record(History *history, const char *url, const char *title);
void history_clear(History *history);
int history_count(const History *history);
const HistoryEntry *history_get(const History *history, int index);
/* Most visited entries, highest count first. Borrowed pointers. */
GPtrArray *history_top_sites(const History *history, guint max);
