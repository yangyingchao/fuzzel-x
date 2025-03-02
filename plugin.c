#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "char32.h"

/**
 * l10n_plugin_init() -- Load and initialize the plugin.
 *
 * This function will be called with the path of the plugin; the plugin may
   use this path to find necessary resources.
 *
 * This function should return NULL upon success, or return a string that
 * describes the reason for failure. The returned string not be freed.
 */
typedef const char *(*l10n_plugin_init)(const char *);

/**
 * translate input string into other forms.
 */
typedef const char *(*l10n_plugin_translate)(const char *);

static void *l10n_handle = NULL;
static l10n_plugin_translate l10n_func = NULL;

void
l10n_plugin_load(const char *path)
{
    l10n_handle = dlopen(path, RTLD_LAZY);
    if (!l10n_handle) {
        fprintf(stderr, "failed to load pinyin plugin\n");
        return;
    }

    dlerror();

    l10n_plugin_init init = dlsym(l10n_handle, "l10n_plugin_init");
    char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "%s\n", error);
        l10n_func = NULL;
    }

    if (!init(path)) {
        fprintf(stderr, "failed to init plugin: %s\n", error);
        l10n_func = NULL;
    }

    l10n_func = dlsym(l10n_handle, "l10n_plugin_translate");
    error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "%s\n", error);
        l10n_func = NULL;
    }
}

char32_t *
l10n_translate(const char *src)
{
    if (!l10n_func) {
        return NULL;
    }

    const char *translated = l10n_func(src);
    if (translated) {
        return ambstoc32(translated);
    }

    return NULL;
}
