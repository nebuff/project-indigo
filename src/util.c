#include "util.h"

#include <string.h>

#include <glib.h>

char *
uri_host(const char *uri)
{
    if (uri == NULL)
        return NULL;

    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (parsed == NULL)
        return NULL;

    const char *name = g_uri_get_host(parsed);
    char *host = (name != NULL && *name != '\0') ? g_strdup(name) : NULL;
    g_uri_unref(parsed);

    if (host != NULL && g_str_has_prefix(host, "www."))
        memmove(host, host + 4, strlen(host + 4) + 1);

    return host;
}
