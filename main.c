#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <threads.h>

#include <locale.h>
#include <libgen.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <dirent.h>

#include <tllist.h>
#include <fcft/fcft.h>

#define LOG_MODULE "fuzzel"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "application.h"
#include "char32.h"
#include "dmenu.h"
#include "fdm.h"
#include "match.h"
#include "render.h"
#include "shm.h"
#include "version.h"
#include "wayland.h"
#include "xdg.h"

struct context {
    struct wayland *wayl;
    struct render *render;
    struct matches *matches;
    struct prompt *prompt;
    struct application_list *apps;

    icon_theme_list_t *themes;
    int icon_size;
    mtx_t *icon_lock;

    struct {
        const char *icon_theme;
        const char *terminal;
        bool icons_enabled;
        bool actions_enabled;
        bool dmenu_mode;
    } options;

    int event_fd;
    int dmenu_abort_fd;
};

static struct rgba
hex_to_rgba(uint32_t color)
{
    return (struct rgba){
        .r = (double)((color >> 24) & 0xff) / 255.0,
        .g = (double)((color >> 16) & 0xff) / 255.0,
        .b = (double)((color >>  8) & 0xff) / 255.0,
        .a = (double)((color >>  0) & 0xff) / 255.0,
    };
}

static void
read_cache(struct application_list *apps)
{
    const char *path = xdg_cache_dir();
    if (path == NULL) {
        LOG_WARN("failed to get cache directory: not saving popularity cache");
        return;
    }

    int cache_dir_fd = open(path, O_DIRECTORY);
    if (cache_dir_fd == -1) {
        LOG_ERRNO("%s: failed to open", path);
        return;
    }

    struct stat st;
    int fd = -1;

    if (fstatat(cache_dir_fd, "fuzzel", &st, 0) == -1 ||
        (fd = openat(cache_dir_fd, "fuzzel", O_RDONLY)) == -1)
    {
        close(cache_dir_fd);
        if (errno != ENOENT)
            LOG_ERRNO("%s/fuzzel: failed to open", path);
        return;
    }
    close(cache_dir_fd);

    char *text = mmap(
        NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);

    if (text == MAP_FAILED) {
        LOG_ERRNO("%s/fuzzel: failed to mmap", path);
        return;
    }

    size_t app_idx = 0;

    /* Loop lines */
    char *lineptr = NULL;
    for (char *line = strtok_r(text, "\n", &lineptr);
         line != NULL;
         line = strtok_r(NULL, "\n", &lineptr))
    {
        /* Parse each line ("<title>|<count>") */
        char *ptr = NULL;
        const char *title = strtok_r(line, "|", &ptr);
        const char *count_str = strtok_r(NULL, "|", &ptr);

        if (title == NULL || count_str == NULL) {
            LOG_ERR("invalid cache entry (cache corrupt?): %s", line);
            continue;
        }

        int count;
        sscanf(count_str, "%u", &count);

        size_t wlen = mbstoc32(NULL, title, 0);
        if (wlen == (size_t)-1)
            continue;

        char32_t wtitle[wlen + 1];
        mbstoc32(wtitle, title, wlen + 1);

        for (; app_idx < apps->count; app_idx++) {
            int cmp = c32cmp(apps->v[app_idx].title, wtitle);

            if (cmp == 0) {
                apps->v[app_idx].count = count;
                app_idx++;
                break;
            } else if (cmp > 0)
                break;
        }
    }

    munmap(text, st.st_size);
}

static void
write_cache(const struct application_list *apps)
{
    const char *path = xdg_cache_dir();
    if (path == NULL) {
        LOG_WARN("failed to get cache directory: not saving popularity cache");
        return;
    }

    int cache_dir_fd = open(path, O_DIRECTORY);
    if (cache_dir_fd == -1) {
        LOG_ERRNO("%s: failed to open", path);
        return;
    }

    int fd = openat(cache_dir_fd, "fuzzel", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    close(cache_dir_fd);

    if (fd == -1) {
        LOG_ERRNO("%s/fuzzel: failed to open", path);
        return;
    }

    for (size_t i = 0; i < apps->count; i++) {
        if (apps->v[i].count == 0)
            continue;

        char count_as_str[11];
        sprintf(count_as_str, "%u", apps->v[i].count);
        const size_t count_len = strlen(count_as_str);

        size_t clen = c32tombs(NULL, apps->v[i].title, 0);
        char ctitle[clen + 1];
        c32tombs(ctitle, apps->v[i].title, clen + 1);

        if (write(fd, ctitle, clen) != clen ||
            write(fd, "|", 1) != 1 ||
            write(fd, count_as_str, count_len) != count_len ||
            write(fd, "\n", 1) != 1)
        {
            LOG_ERRNO("failed to write cache");
            break;
        }
    }

    close(fd);
}

static void
print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTION]...\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("  -o,--output=OUTPUT             output (monitor) to display on (none)\n"
           "  -f,--font=FONT                 font name and style, in FontConfig format\n"
           "                                 (monospace)\n"
           "  -i,--icon-theme=NAME           icon theme name (\"hicolor\")\n"
           "  -I,--no-icons                  do not render any icons\n"
           "  -F,--fields=FIELDS             comma separated list of XDG Desktop entry\n"
           "                                 fields to match\n"
           "  -T,--terminal                  terminal command to use when launching\n"
           "                                 'terminal' programs, e.g. \"xterm -e\".\n"
           "                                 Not used in dmenu mode (not set)\n"
           "  -l,--lines                     number of matches to show\n"
           "  -w,--width                     window width, in characters (margins and\n"
           "                                 borders not included)\n"
           "  -x,--horizontal-pad=PAD        horizontal padding, in pixels (40)\n"
           "  -y,--vertical-pad=PAD          vertical padding, in pixels (8)\n"
           "  -p,--inner-pad=PAD             vertical padding between prompt and match list,\n"
           "                                 in pixels (0)\n"
           "  -b,--background-color=HEX      background color (000000ff)\n"
           "  -t,--text-color=HEX            text color (ffffffff)\n"
           "  -m,--match-color=HEX           color of matched substring (cc9393ff)\n"
           "  -s,--selection-color=HEX       background color of selected item (333333ff)\n"
           "  -S,--selection-text-color=HEX  text color of selected item (ffffffff)\n"
           "  -B,--border-width=INT          width of border, in pixels (1)\n"
           "  -r,--border-radius=INT         amount of corner \"roundness\" (10)\n"
           "  -C,--border-color=HEX          border color (ffffffff)\n"
           "     --show-actions              include desktop actions in the list\n"
           "     --no-fuzzy                  disable fuzzy matching\n"
           "     --fuzzy-min-length=VALUE    search strings shorter than this will not be\n"
           "                                 fuzzy matched (3)\n"
           "     --fuzzy-max-length-discrepancy=VALUE  maximum allowed length discrepancy\n"
           "                                           between a fuzzy match and the search\n"
           "                                           criteria. Larger values mean more\n"
           "                                           fuzzy matches (2)\n"
           "     --fuzzy-max-distance=VALUE  maximum levenshtein distance between a fuzzy (1)\n"
           "                                 match and the search criteria. Larger values\n"
           "                                 mean more fuzzy matches\n"
           "     --line-height=HEIGHT        override line height from font metrics\n"
           "     --letter-spacing=AMOUNT     additional letter spacing\n"
           "     --launch-prefix=COMMAND     prefix to add before argv of executed program\n"
           "  -d,--dmenu                     dmenu compatibility mode\n"
           "     --index                     print selected entry's index instead of of the \n"
           "                                 entry's text (dmenu mode only)\n"
           "  -R,--no-run-if-empty           exit immediately without showing UI if stdin\n"
           "                                 is empty (dmenu mode only)\n"
           "     --log-level={info|warning|error|none}\n"
           "                                 log level (info)\n"
           "     --log-colorize=[never|always|auto]\n"
           "                                 enable/disable colorization of log output on\n"
           "                                 stderr\n"
           "     --log-no-syslog             disable syslog logging\n"
           "  -v,--version                   show the version number and quit\n");
    printf("\n");
    printf("All colors are RGBA - i.e. 8-digit hex values, without prefix.\n");
}

static void
font_reloaded(struct wayland *wayl, struct fcft_font *font, void *data)
{
    struct context *ctx = data;

    applications_flush_text_run_cache(ctx->apps);

    mtx_lock(ctx->icon_lock);
    {
        ctx->icon_size = font->height;
        if (ctx->options.icons_enabled) {
            icon_reload_application_icons(
                *ctx->themes, ctx->icon_size, ctx->apps);
        }
    }
    mtx_unlock(ctx->icon_lock);
}

static bool
pt_or_px_from_string(const char  *s, struct pt_or_px *res)
{
    const size_t len = strlen(s);
    if (len >= 2 && s[len - 2] == 'p' && s[len - 1] == 'x') {
        errno = 0;
        char *end = NULL;

        long value = strtol(s, &end, 10);
        if (!(errno == 0 && end == s + len - 2)) {
            fprintf(stderr, "%s: not a valid pixel value\n", s);
            return false;
        }

        res->pt = 0;
        res->px = value;
    } else {
        errno = 0;
        char *end = NULL;

        float value = strtof(s, &end);
        if (!(errno == 0 && *end == '\0')) {
            fprintf(stderr, "%s: not a valid point value\n", s);
            return false;
        }
        res->pt = value;
        res->px = 0;
    }

    return true;
}

static char *
lock_file_name(void)
{
    const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime_dir == NULL)
        xdg_runtime_dir = "/tmp";

    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display == NULL)
        return NULL;

    #define path_fmt "%s/fuzzel-%s.lock"
    int chars = snprintf(NULL, 0, path_fmt, xdg_runtime_dir, wayland_display);

    char *path = malloc(chars + 1);
    snprintf(path, chars + 1, path_fmt, xdg_runtime_dir, wayland_display);
    #undef path_fmt

    LOG_DBG("lock file: %s", path);
    return path;
}

static bool
acquire_file_lock(const char *path, int *fd)
{
    *fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (*fd < 0) {
        /* Warn, but allow running anyway */
        LOG_WARN("%s: failed to create lock file", path);
        return true;
    }

    if (flock(*fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK) {
            /* The file is locked and the LOCK_NB flag was selected */
            LOG_ERR("%s: failed to acquire lock: fuzzel already running?", path);
            return false;
        }
    }

    return true;
}

enum event_type { EVENT_APPS_LOADED = 1, EVENT_ICONS_LOADED = 2 };

static int
send_event(int fd, enum event_type event)
{
    ssize_t bytes = write(fd, &(uint64_t){event}, sizeof(uint64_t));

    if (bytes < 0)
        return -errno;
    else if (bytes != (ssize_t)sizeof(uint64_t))
        return 1;
    return 0;
}


/* THREAD */
static int
populate_apps(void *_ctx)
{
    struct context *ctx = _ctx;
    struct application_list *apps = ctx->apps;
    const char *icon_theme = ctx->options.icon_theme;
    const char *terminal = ctx->options.terminal;
    bool actions_enabled = ctx->options.actions_enabled;
    bool dmenu_mode = ctx->options.dmenu_mode;
    bool icons_enabled = ctx->options.icons_enabled;

    if (dmenu_mode) {
        dmenu_load_entries(apps, ctx->dmenu_abort_fd);
        return send_event(ctx->event_fd, EVENT_APPS_LOADED);
    }

    xdg_find_programs(terminal, actions_enabled, apps);
    read_cache(apps);

    int r = send_event(ctx->event_fd, EVENT_APPS_LOADED);
    if (r != 0)
        return r;

    if (icons_enabled) {
        icon_theme_list_t icon_themes = icon_load_theme(icon_theme);
        if (tll_length(icon_themes) > 0)
            LOG_INFO("theme: %s", tll_front(icon_themes).name);
        else
            LOG_WARN("%s: icon theme not found", icon_theme);

        mtx_lock(ctx->icon_lock);
        {
            *ctx->themes = icon_themes;
            if (ctx->icon_size > 0) {
                icon_reload_application_icons(
                    *ctx->themes, ctx->icon_size, apps);
            }
        }
        mtx_unlock(ctx->icon_lock);

        r = send_event(ctx->event_fd, EVENT_ICONS_LOADED);
        if (r != 0)
            return r;
    }

    return 0;
}

/*
 * Called when the application list has been populated
 */
static bool
fdm_apps_populated(struct fdm *fdm, int fd, int events, void *data)
{
    if (events & EPOLLHUP)
        return false;

    uint64_t event;
    ssize_t bytes = read(fd, &event, sizeof(event));
    if (bytes != (ssize_t)sizeof(event)) {
        if (bytes < 0)
            LOG_ERRNO("failed to read event FD");
        else
            LOG_ERR("partial read from event FD");
        return false;
    }

    struct context *ctx = data;
    struct wayland *wayl = ctx->wayl;
    struct application_list *apps = ctx->apps;
    struct matches *matches = ctx->matches;
    struct prompt *prompt = ctx->prompt;

    switch (event) {
    case EVENT_APPS_LOADED:
        /* Update matches list, then refresh the GUI */
        matches_set_applications(matches, apps);
        matches_update(matches, prompt);
        break;

    case EVENT_ICONS_LOADED:
        /* Just need to refresh the GUI */
        break;

    default:
        LOG_ERR("unknown event: %llx", (long long)event);
        return false;
    }

    wayl_refresh(wayl);
    return true;
}

int
main(int argc, char *const *argv)
{
    #define OPT_LETTER_SPACING               256
    #define OPT_LAUNCH_PREFIX                257
    #define OPT_SHOW_ACTIONS                 258
    #define OPT_NO_FUZZY                     259
    #define OPT_FUZZY_MIN_LENGTH             260
    #define OPT_FUZZY_MAX_LENGTH_DISCREPANCY 261
    #define OPT_FUZZY_MAX_DISTANCE           262
    #define OPT_DMENU_INDEX                  263
    #define OPT_LOG_LEVEL                    264
    #define OPT_LOG_COLORIZE                 265
    #define OPT_LOG_NO_SYSLOG                266

    static const struct option longopts[] = {
        {"output"  ,             required_argument, 0, 'o'},
        {"font",                 required_argument, 0, 'f'},
        {"dpi-aware",            required_argument, 0, 'D'},
        {"icon-theme",           required_argument, 0, 'i'},
        {"no-icons",             no_argument,       0, 'I'},
        {"fields",               required_argument, 0, 'F'},
        {"lines",                required_argument, 0, 'l'},
        {"width",                required_argument, 0, 'w'},
        {"horizontal-pad",       required_argument, 0, 'x'},
        {"vertical-pad",         required_argument, 0, 'y'},
        {"inner-pad",            required_argument, 0, 'p'},
        {"background-color",     required_argument, 0, 'b'},
        {"text-color",           required_argument, 0, 't'},
        {"match-color",          required_argument, 0, 'm'},
        {"selection-color",      required_argument, 0, 's'},
        {"selection-text-color", required_argument, 0, 'S'},
        {"border-width",         required_argument, 0, 'B'},
        {"border-radius",        required_argument, 0, 'r'},
        {"border-color",         required_argument, 0, 'C'},
        {"prompt",               required_argument, 0, 'P'},
        {"terminal",             required_argument, 0, 'T'},
        {"show-actions",         no_argument,       0, OPT_SHOW_ACTIONS},
        {"no-fuzzy",             no_argument,       0, OPT_NO_FUZZY},
        {"fuzzy-min-length",     required_argument, 0, OPT_FUZZY_MIN_LENGTH},
        {"fuzzy-max-length-discrepancy", required_argument, 0, OPT_FUZZY_MAX_LENGTH_DISCREPANCY},
        {"fuzzy-max-distance",   required_argument, 0, OPT_FUZZY_MAX_DISTANCE},
        {"line-height",          required_argument, 0, 'H'},
        {"letter-spacing",       required_argument, 0, OPT_LETTER_SPACING},
        {"launch-prefix",        required_argument, 0, OPT_LAUNCH_PREFIX},

        /* dmenu mode */
        {"dmenu",                no_argument,       0, 'd'},
        {"no-run-if-empty",      no_argument,       0, 'R'},
        {"index",                no_argument,       0, OPT_DMENU_INDEX},

        /* Misc */
        {"log-level",            required_argument, 0, OPT_LOG_LEVEL},
        {"log-colorize",         optional_argument, 0, OPT_LOG_COLORIZE},
        {"log-no-syslog",        no_argument,       0, OPT_LOG_NO_SYSLOG},
        {"version",              no_argument,       0, 'v'},
        {"help",                 no_argument,       0, 'h'},
        {NULL,                   no_argument,       0, 0},
    };

    enum dpi_aware dpi_aware = DPI_AWARE_AUTO;
    const char *output_name = NULL;
    const char *font_name = "monospace";
    const char *icon_theme = "hicolor";
    const char *terminal = NULL;
    const char32_t *prompt_content = U"> ";
    char32_t *prompt_allocated = NULL;
    bool icons_enabled = true;
    bool actions_enabled = false;
    bool fuzzy = true;
    size_t fuzzy_min_length = 3;
    size_t fuzzy_max_length_discrepancy = 2;
    size_t fuzzy_max_distance = 1;
    const char *launch_prefix = NULL;

    enum log_class log_level = LOG_CLASS_INFO;
    enum log_colorize log_colorize = LOG_COLORIZE_AUTO;
    bool log_syslog = true;

    bool dmenu_mode = false;
    enum dmenu_mode dmenu_format = DMENU_MODE_TEXT;
    bool no_run_if_empty = false;

    enum match_fields match_fields =
        MATCH_FILENAME | MATCH_NAME | MATCH_GENERIC;

    struct render_options render_options = {
        .lines = 15,
        .chars = 30,
        .border_size = 1u,
        .border_radius = 10u,
        .pad = {.x = 40, .y = 8, .inner = 0},
        .background_color = hex_to_rgba(0xfdf6e3dd),
        .border_color = hex_to_rgba(0x002b36ff),
        .text_color = hex_to_rgba(0x657b83ff),
        .match_color = hex_to_rgba(0xcb4b16ff),
        .selection_color = hex_to_rgba(0xeee8d5dd),
        .selection_text_color = hex_to_rgba(0x657b83ff),
        .line_height = {-1, 0.0},  /* Use font metrics */
        .letter_spacing = {0},
    };

    setlocale(LC_CTYPE, "");

    /* Auto-enable dmenu mode if invoked through a ‘dmenu’ symlink */
    if (argv[0] != NULL) {
        char *copy = strdup(argv[0]);
        if (copy != NULL) {
            const char *name = basename(copy);
            if (name != NULL && strcmp(name, "dmenu") == 0)
                dmenu_mode = true;

            free(copy);
        }
    }

    while (true) {
        int c = getopt_long(argc, argv, ":o:f:D:i:IF:l:w:x:y:p:P:b:t:m:s:S:B:r:C:T:dRvh", longopts, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'o':
            output_name = optarg;
            break;

        case 'f':
            font_name = optarg;
            break;

        case 'D':
            if (strcmp(optarg, "auto") == 0)
                dpi_aware = DPI_AWARE_AUTO;
            else if (strcmp(optarg, "no") == 0)
                dpi_aware = DPI_AWARE_NO;
            else if (strcmp(optarg, "yes") == 0)
                dpi_aware = DPI_AWARE_YES;
            else {
                fprintf(
                    stderr,
                    "%s: invalid value for dpi-aware: "
                    "must be one of 'auto', 'no', or 'yes'\n",
                    optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'i':
            icon_theme = optarg;
            break;

        case 'I':
            icons_enabled = false;
            break;

        case 'F': {
            static const struct {
                const char *name;
                enum match_fields value;
            } map[] = {
                {"filename", MATCH_FILENAME},
                {"name", MATCH_NAME},
                {"generic", MATCH_GENERIC},
                {"exec", MATCH_EXEC},
                {"categories", MATCH_CATEGORIES},
                {"keywords", MATCH_KEYWORDS},
                {"comment", MATCH_COMMENT},
            };

            match_fields = 0;
            for (const char *f = strtok(optarg, ", ");
                 f != NULL;
                 f = strtok(NULL, ", "))
            {
                enum match_fields field = 0;

                for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                    if (strcmp(f, map[i].name) == 0) {
                        field = map[i].value;
                        break;
                    }
                }

                if (field > 0)
                    match_fields |= field;
                else {
                    char valid_names[128] = {0};
                    int idx = 0;
                    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                        idx += snprintf(
                            &valid_names[idx], sizeof(valid_names) - idx,
                            "'%s', ", map[i].name);
                    }

                    valid_names[idx - 2] = '\0';

                    fprintf(
                        stderr,
                        "%s: invalid field name: must be one of %s\n",
                        f, valid_names);
                    return EXIT_FAILURE;
                }
            }
            break;
        }

        case 'T':
            terminal = optarg;
            break;

        case 'l':
            if (sscanf(optarg, "%u", &render_options.lines) != 1) {
                fprintf(stderr, "%s: invalid line count\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'w':
            if (sscanf(optarg, "%u", &render_options.chars) != 1) {
                fprintf(stderr, "%s: invalid width\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'x':
            if (sscanf(optarg, "%u", &render_options.pad.x) != 1) {
                fprintf(stderr, "%s: invalid padding\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'y':
            if (sscanf(optarg, "%u", &render_options.pad.y) != 1) {
                fprintf(stderr, "%s: invalid padding\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'p':
            if (sscanf(optarg, "%u", &render_options.pad.inner) != 1) {
                fprintf(stderr, "%s: invalid padding\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'P':
            free(prompt_allocated);
            prompt_allocated = ambstoc32(optarg);

            if (prompt_allocated == NULL) {
                fprintf(stderr, "%s: invalid prompt\n", optarg);
                return EXIT_FAILURE;
            }

            prompt_content = prompt_allocated;
            break;

        case 'b': {
            uint32_t background;
            if (sscanf(optarg, "%08x", &background) != 1) {
                fprintf(stderr, "%s: invalid color\n", optarg);
                return EXIT_FAILURE;
            }
            render_options.background_color = hex_to_rgba(background);
            break;
        }

        case 't': {
            uint32_t text_color;
            if (sscanf(optarg, "%08x", &text_color) != 1) {
                fprintf(stderr, "%s: invalid color\n", optarg);
                return EXIT_FAILURE;
            }
            render_options.text_color = hex_to_rgba(text_color);
            break;
        }

        case 'm': {
            uint32_t match_color;
            if (sscanf(optarg, "%x", &match_color) != 1) {
                fprintf(stderr, "%s: invalid color\n", optarg);
                return EXIT_FAILURE;
            }
            render_options.match_color = hex_to_rgba(match_color);
            break;
        }

        case 's': {
            uint32_t selection_color;
            if (sscanf(optarg, "%x", &selection_color) != 1) {
                fprintf(stderr, "%s: invalid color\n", optarg);
                return EXIT_FAILURE;
            }
            render_options.selection_color = hex_to_rgba(selection_color);
            break;
        }

        case 'S': {
            uint32_t selection_text_color;
            if (sscanf(optarg, "%x", &selection_text_color) != 1) {
                fprintf(stderr, "%s: invalid color\n", optarg);
                return EXIT_FAILURE;
            }
            render_options.selection_text_color = hex_to_rgba(selection_text_color);
            break;
        }

        case 'B':
            if (sscanf(optarg, "%u", &render_options.border_size) != 1) {
                fprintf(
                    stderr,
                    "%s: invalid border width (must be an integer)\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'r':
            if (sscanf(optarg, "%u", &render_options.border_radius) != 1) {
                fprintf(stderr, "%s: invalid border radius (must be an integer)\n",
                        optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'C': {
            uint32_t border_color;
            if (sscanf(optarg, "%x", &border_color) != 1) {
                fprintf(stderr, "%s: invalid color\n", optarg);
                return EXIT_FAILURE;
            }
            render_options.border_color = hex_to_rgba(border_color);
            break;
        }

        case OPT_SHOW_ACTIONS:
            actions_enabled = true;
            break;

        case OPT_NO_FUZZY:
            fuzzy = false;
            break;

        case OPT_FUZZY_MIN_LENGTH:
            if (sscanf(optarg, "%zu", &fuzzy_min_length) != 1) {
                fprintf(
                    stderr,
                    "%s: invalid fuzzy min length (must be an integer)\n",
                    optarg);
                return EXIT_FAILURE;
            }
            break;

        case OPT_FUZZY_MAX_LENGTH_DISCREPANCY:
            if (sscanf(optarg, "%zu", &fuzzy_max_length_discrepancy) != 1) {
                fprintf(
                    stderr,
                    "%s: invalid fuzzy max length discrepancy "
                    "(must be an integer)\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case OPT_FUZZY_MAX_DISTANCE:
            if (sscanf(optarg, "%zu", &fuzzy_max_distance) != 1) {
                fprintf(
                    stderr,
                    "%s: invalid fuzzy max distance (must be an integer)\n",
                    optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'H': { /* line-height */
            if (!pt_or_px_from_string(optarg, &render_options.line_height))
                return EXIT_FAILURE;
            break;
        }

        case OPT_LETTER_SPACING: {
            if (!pt_or_px_from_string(optarg, &render_options.letter_spacing))
                return EXIT_FAILURE;
            break;
        }

        case OPT_LAUNCH_PREFIX: {
            launch_prefix = optarg;
            break;
        }

        case 'd':
            dmenu_mode = true;
            break;

        case 'R':
            no_run_if_empty = true;
            break;

        case OPT_DMENU_INDEX:
            dmenu_format = DMENU_MODE_INDEX;
            break;

        case OPT_LOG_LEVEL: {
            int lvl = log_level_from_string(optarg);
            if (lvl < 0) {
                fprintf(
                    stderr,
                    "--log-level: %s: argument must be one of %s\n",
                    optarg, log_level_string_hint());
                return EXIT_FAILURE;
            }
            log_level = lvl;
            break;
        }

        case OPT_LOG_COLORIZE:
            if (optarg == NULL || strcmp(optarg, "auto") == 0)
                log_colorize = LOG_COLORIZE_AUTO;
            else if (strcmp(optarg, "never") == 0)
                log_colorize = LOG_COLORIZE_NEVER;
            else if (strcmp(optarg, "always") == 0)
                log_colorize = LOG_COLORIZE_ALWAYS;
            else {
                fprintf(stderr, "--log-colorize: %s: argument must be one of 'never', 'always' or 'auto'\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case OPT_LOG_NO_SYSLOG:
            log_syslog = false;
            break;

        case 'v':
            printf("fuzzel version %s\n", FUZZEL_VERSION);
            return EXIT_SUCCESS;

        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;

        case ':':
            fprintf(stderr, "error: -%c: missing required argument\n", optopt);
            return EXIT_FAILURE;

        case '?':
            fprintf(stderr, "error: -%c: invalid option\n", optopt);
            return EXIT_FAILURE;
        }
    }

    int ret = EXIT_FAILURE;

    _Static_assert((int)LOG_CLASS_ERROR == (int)FCFT_LOG_CLASS_ERROR,
                   "fcft log level enum offset");
    _Static_assert((int)LOG_COLORIZE_ALWAYS == (int)FCFT_LOG_COLORIZE_ALWAYS,
                   "fcft colorize enum mismatch");

    log_init(log_colorize, log_syslog, LOG_FACILITY_USER, log_level);
    fcft_init((enum fcft_log_colorize)log_colorize, log_syslog, (enum fcft_log_class)log_level);

#if !defined(FUZZEL_ENABLE_SVG_LIBRSVG)
    /* Skip fcft cleanup if we’re using the librsvg backend
     * (https://codeberg.org/dnkl/fuzzel/issues/87) */
    atexit(&fcft_fini);
#endif

    mtx_t icon_lock;
    if (mtx_init(&icon_lock, mtx_plain) != thrd_success) {
        LOG_ERR("failed to create icon lock");
        return EXIT_FAILURE;
    }

    struct application_list *apps = NULL;
    struct fdm *fdm = NULL;
    struct prompt *prompt = NULL;
    struct matches *matches = NULL;
    struct render *render = NULL;
    struct wayland *wayl = NULL;

    thrd_t app_thread_id;
    bool join_app_thread = false;
    int event_pipe[2] = {-1, -1};
    int dmenu_abort_fd = -1;

    char *lock_file = NULL;
    int file_lock_fd = -1;
    bool unlink_lock_file = true;

    icon_theme_list_t themes = tll_init();

    /* Don’t allow multiple instances (in the same Wayland session) */
    lock_file = lock_file_name();
    if (lock_file != NULL) {
        if (!acquire_file_lock(lock_file, &file_lock_fd)) {
            unlink_lock_file = false;
            goto out;
        }
    }

    if ((fdm = fdm_init()) == NULL)
        goto out;

    if ((render = render_init(&render_options, &icon_lock)) == NULL)
        goto out;

    if ((prompt = prompt_init(prompt_content)) == NULL)
        goto out;

    if ((matches = matches_init(
             match_fields, fuzzy, fuzzy_min_length,
             fuzzy_max_length_discrepancy, fuzzy_max_distance)) == NULL)
        goto out;
    matches_max_matches_per_page_set(matches, render_options.lines);

    if ((apps = applications_init()) == NULL)
        goto out;

    if (dmenu_mode && no_run_if_empty) {
        /*
         * If no_run_if_empty is set, we *must* load the entries
         * *before displaying the window.
         */
        dmenu_load_entries(apps, -1);
        if (apps->count == 0)
            goto out;

        matches_set_applications(matches, apps);
        matches_update(matches, prompt);
    } else if (dmenu_mode) {
        dmenu_abort_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (dmenu_abort_fd < 0) {
            LOG_ERRNO("failed to create event FD for dmenu mode");
            goto out;
        }
    }

    struct context ctx = {
        .render = render,
        .matches = matches,
        .prompt = prompt,
        .apps = apps,
        .themes = &themes,
        .icon_lock = &icon_lock,
        .options = {
            .icon_theme = icon_theme,
            .terminal = terminal,
            .icons_enabled = icons_enabled,
            .actions_enabled = actions_enabled,
            .dmenu_mode = dmenu_mode,
        },
        .event_fd = -1,
        .dmenu_abort_fd = dmenu_abort_fd,
    };

    if ((wayl = wayl_init(
             fdm, render, prompt, matches, &render_options,
             dmenu_mode ? dmenu_format : DMENU_MODE_NONE,
             launch_prefix, output_name, font_name, dpi_aware,
             &font_reloaded, &ctx)) == NULL)
        goto out;

    ctx.wayl = wayl;

    /* Create thread that will populate the application list */
    if (!dmenu_mode || !no_run_if_empty) {
        if (pipe2(event_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
            LOG_ERRNO("failed to create event pipe");
            goto out;
        }

        ctx.event_fd = event_pipe[1];

        if (!fdm_add(fdm, event_pipe[0], EPOLLIN, &fdm_apps_populated, &ctx))
            goto out;

        if (thrd_create(&app_thread_id, &populate_apps, &ctx) != thrd_success) {
            LOG_ERR("failed to create thread");
            goto out;
        }

        join_app_thread = true;
    }

    wayl_refresh(wayl);

    while (true) {
        wayl_flush(wayl);
        if (!fdm_poll(fdm))
            break;
    }

    if (wayl_update_cache(wayl))
        write_cache(apps);

    ret = wayl_exit_code(wayl);

out:
    if (join_app_thread) {
        if (dmenu_abort_fd >= 0) {
            if (write(dmenu_abort_fd, &(uint64_t){1}, sizeof(uint64_t)) < 0)
                LOG_ERRNO("failed to signal abort");
        }

        int res;
        thrd_join(app_thread_id, &res);

        if (res != 0) {
            if (res < 0)
                LOG_ERRNO_P("populate application list thread failed", res);
            else
                LOG_ERRNO("populate application list thread failed: "
                          "failed to signal done event");
        }
    }

    if (event_pipe[0] >= 0) {
        fdm_del_no_close(fdm, event_pipe[0]);
        close(event_pipe[0]);
    }
    if (event_pipe[1] >= 0)
        close(event_pipe[1]);

    if (dmenu_abort_fd >= 0)
        close(dmenu_abort_fd);

    mtx_destroy(&icon_lock);

    shm_fini();

    wayl_destroy(wayl);
    render_destroy(render);
    matches_destroy(matches);
    prompt_destroy(prompt);
    fdm_destroy(fdm);
    applications_destroy(apps);
    icon_themes_destroy(themes);
    free(prompt_allocated);

#if defined(FUZZEL_ENABLE_CAIRO) && defined(_DEBUG)
    cairo_debug_reset_static_data();
#endif
    log_deinit();

    if (file_lock_fd >= 0)
        close(file_lock_fd);
    if (lock_file != NULL) {
        if (unlink_lock_file)
            unlink(lock_file);
        free(lock_file);
    }
    return ret;
}
