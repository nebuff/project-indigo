#pragma once

typedef struct _SearchHistory SearchHistory;

/* Recent searches, newest first, persisted in the user's state directory. */
SearchHistory *search_history_load(void);
void search_history_free(SearchHistory *history);
void search_history_add(SearchHistory *history, const char *query);
int search_history_count(const SearchHistory *history);
const char *search_history_get(const SearchHistory *history, int index);
