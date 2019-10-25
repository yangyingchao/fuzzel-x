#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <wctype.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include <locale.h>

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>

#include <cairo.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <xdg-output-unstable-v1.h>
#include <wlr-layer-shell-unstable-v1.h>

#define LOG_MODULE "fuzzel"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "application.h"
#include "dmenu.h"
#include "fdm.h"
#include "font.h"
#include "kbd-repeater.h"
#include "match.h"
#include "render.h"
#include "shm.h"
#include "tllist.h"
#include "version.h"
#include "wayland.h"
#include "xdg.h"

#if 0
struct monitor {
    struct wl_output *output;
    struct zxdg_output_v1 *xdg;
    char *name;

    int x;
    int y;

    int width_mm;
    int height_mm;

    int width_px;
    int height_px;

    int scale;
};

struct wayland {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct zxdg_output_manager_v1 *xdg_output_manager;

    tll(struct monitor) monitors;
    const struct monitor *monitor;

    struct xkb_context *xkb;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
};
#endif
struct context {
    volatile enum { KEEP_RUNNING, EXIT_UPDATE_CACHE, EXIT} status;
    int exit_code;

    //struct wayland wl;
    struct wayland *wayl;
    struct render *render;
    struct repeat *repeat;

    bool dmenu_mode;
    struct prompt prompt;
    struct application_list applications;

    struct match *matches;
    size_t match_count;
    size_t selected;

    bool frame_is_scheduled;
    struct buffer *pending;

    /* Window configuration */
    int width;
    int height;
};

static int max_matches;

static void refresh(struct context *c);
static void update_matches(struct context *c);

static int
sort_match_by_count(const void *_a, const void *_b)
{
    const struct match *a = _a;
    const struct match *b = _b;
    return b->application->count - a->application->count;
}

static wchar_t *
wcscasestr(const wchar_t *haystack, const wchar_t *needle)
{
    const size_t hay_len = wcslen(haystack);
    const size_t needle_len = wcslen(needle);

    if (needle_len > hay_len)
        return NULL;

    for (size_t i = 0; i < wcslen(haystack) - wcslen(needle) + 1; i++) {
        bool matched = true;
        for (size_t j = 0; j < wcslen(needle); j++) {
            if (towlower(haystack[i + j]) != towlower(needle[j])) {
                matched = false;
                break;
            }
        }

        if (matched)
            return (wchar_t *)&haystack[i];
    }

    return NULL;
}

static void
update_matches(struct context *c)
{
    /* Nothing entered; all programs found matches */
    if (wcslen(c->prompt.text) == 0) {

        for (size_t i = 0; i < c->applications.count; i++) {
            c->matches[i] = (struct match){
                .application = &c->applications.v[i],
                .start_title = -1,
                .start_comment = -1};
        }

        /* Sort */
        c->match_count = c->applications.count;
        qsort(c->matches, c->match_count, sizeof(c->matches[0]), &sort_match_by_count);

        /* Limit count (don't render outside window) */
        if (c->match_count > max_matches)
            c->match_count = max_matches;

        if (c->selected >= c->match_count && c->selected > 0)
            c->selected = c->match_count - 1;
        return;
    }

    c->match_count = 0;
    for (size_t i = 0; i < c->applications.count; i++) {
        struct application *app = &c->applications.v[i];
        size_t start_title = -1;
        size_t start_comment = -1;

        const wchar_t *m = wcscasestr(app->title, c->prompt.text);
        if (m != NULL)
            start_title = m - app->title;

        if (app->comment != NULL) {
            m = wcscasestr(app->comment, c->prompt.text);
            if (m != NULL)
                start_comment = m - app->comment;
        }

        if (start_title == -1 && start_comment == -1)
            continue;

        c->matches[c->match_count++] = (struct match){
            .application = app,
            .start_title = start_title,
            .start_comment = start_comment};
    }

    /* Sort */
    qsort(c->matches, c->match_count, sizeof(c->matches[0]), &sort_match_by_count);

    /* Limit count (don't render outside window) */
    if (c->match_count > max_matches)
        c->match_count = max_matches;

    if (c->selected >= c->match_count && c->selected > 0)
        c->selected = c->match_count - 1;
}

static void frame_callback(
    void *data, struct wl_callback *wl_callback, uint32_t callback_data);

static const struct wl_callback_listener frame_listener = {
    .done = &frame_callback,
};

static void
commit_buffer(struct context *c, struct buffer *buf)
{
    assert(buf->busy);

    wl_surface_set_buffer_scale(c->wl.surface, c->wl.monitor->scale);
    wl_surface_attach(c->wl.surface, buf->wl_buf, 0, 0);
    wl_surface_damage(c->wl.surface, 0, 0, buf->width, buf->height);

    struct wl_callback *cb = wl_surface_frame(c->wl.surface);
    wl_callback_add_listener(cb, &frame_listener, c);

    c->frame_is_scheduled = true;
    wl_surface_commit(c->wl.surface);
}

static void
frame_callback(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
    struct context *c = data;

    c->frame_is_scheduled = false;
    wl_callback_destroy(wl_callback);

    if (c->pending != NULL) {
        commit_buffer(c, c->pending);
        c->pending = NULL;
    }
}

static void
refresh(struct context *c)
{
    LOG_DBG("refresh");

    struct buffer *buf = shm_get_buffer(c->wl.shm, c->width, c->height);

    /* Background + border */
    render_background(c->render, buf);

    /* Window content */
    render_prompt(c->render, buf, &c->prompt);
    render_match_list(c->render, buf, c->matches, c->match_count,
                      wcslen(c->prompt.text), c->selected);

    cairo_surface_flush(buf->cairo_surface);

    if (c->frame_is_scheduled) {
        /* There's already a frame being drawn - delay current frame
         * (overwriting any previous pending frame) */

        if (c->pending != NULL)
            c->pending->busy = false;

        c->pending = buf;
    } else {
        /* No pending frames - render immediately */
        assert(c->pending == NULL);
        commit_buffer(c, buf);
    }
}

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

        int count;
        sscanf(count_str, "%u", &count);

        size_t wlen = mbstowcs(NULL, title, 0);
        wchar_t wtitle[wlen + 1];
        mbstowcs(wtitle, title, wlen + 1);

        for (; app_idx < apps->count; app_idx++) {
            int cmp = wcscmp(apps->v[app_idx].title, wtitle);

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

        size_t clen = wcstombs(NULL, apps->v[i].title, 0);
        char ctitle[clen + 1];
        wcstombs(ctitle, apps->v[i].title, clen + 1);


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

static bool
fdm_wayl(struct fdm *fdm, int fd, int events, void *data)
{
    struct context *c = data;
    int event_count = wl_display_dispatch(c->wl.display);

    if (events & EPOLLHUP) {
        LOG_WARN("disconnected from Wayland");
        return false;
    }

    return event_count != -1;
}

static void
repeat_cb(uint32_t key, void *data)
{
    struct context *c = data;
    keyboard_key(c, NULL, 0, 0, key, XKB_KEY_DOWN);
}

static void
print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTION]...\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("  -o,--output=OUTPUT         output (monitor) to display on (none)\n"
           "  -f,--font=FONT             font name and style in fontconfig format (monospace)\n"
           "  -i,--icon-theme=NAME       icon theme name (\"hicolor\")\n"
           "  -g,--geometry=WxH          window WIDTHxHEIGHT, in pixels (500x300)\n"
           "  -b,--background-color=HEX  background color (000000ff)\n"
           "  -t,--text-color=HEX        text color (ffffffff)\n"
           "  -m,--match-color=HEX       color of matched substring (cc9393ff)\n"
           "  -s,--selection-color=HEX   background color of selected item (333333ff)\n"
           "  -B,--border-width=INT      width of border, in pixels (1)\n"
           "  -r,--border-radius=INT     amount of corner \"roundness\" (10)\n"
           "  -C,--border-color=HEX      border color (ffffffff)\n"
           "  -d,--dmenu                 dmenu compatibility mode\n"
           "  -v,--version               show the version number and quit\n");
    printf("\n");
    printf("Colors must be specified as a 32-bit hexadecimal RGBA quadruple.\n");
}

int
main(int argc, char *const *argv)
{
    static const struct option longopts[] = {
        {"output"  ,         required_argument, 0, 'o'},
        {"font",             required_argument, 0, 'f'},
        {"icon-theme",       required_argument, 0, 'i'},
        {"geometry",         required_argument, 0, 'g'},
        {"background-color", required_argument, 0, 'b'},
        {"text-color",       required_argument, 0, 't'},
        {"match-color",      required_argument, 0, 'm'},
        {"selection-color",  required_argument, 0, 's'},
        {"border-width",     required_argument, 0, 'B'},
        {"border-radius",    required_argument, 0, 'r'},
        {"border-color",     required_argument, 0, 'C'},
        {"dmenu",            no_argument,       0, 'd'},
        {"version",          no_argument,       0, 'v'},
        {"help",             no_argument,       0, 'h'},
        {NULL,               no_argument,       0, 0},
    };

    const char *output_name = NULL;
    const char *font_name = "monospace";
    const char *icon_theme = "hicolor";

    int width = 500;
    int height = 300;
    const int x_margin = 20;
    const int y_margin = 4;
    int border_width = 1;
    int border_radius = 10;
    uint32_t text_color = 0xffffffff;
    uint32_t match_color = 0xcc9393ff;
    uint32_t background = 0x000000ff;
    uint32_t selection_color = 0x333333ff;
    uint32_t border_color = 0xffffffff;
    bool dmenu_mode = false;

    while (true) {
        int c = getopt_long(argc, argv, ":o:f:i:g:b:t:m:s:B:r:C:dvh", longopts, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'o':
            output_name = optarg;
            break;

        case 'f':
            font_name = optarg;
            break;

        case 'i':
            icon_theme = optarg;
            break;

        case 'g':
            if (sscanf(optarg, "%dx%d", &width, &height) != 2) {
                LOG_ERR("%s: invalid geometry (must be <width>x<height>)", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'b':
            if (sscanf(optarg, "%08x", &background) != 1) {
                LOG_ERR("%s: invalid color", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 't':
            if (sscanf(optarg, "%08x", &text_color) != 1) {
                LOG_ERR("%s: invalid color", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'm':
            if (sscanf(optarg, "%x", &match_color) != 1) {
                LOG_ERR("%s: invalid color", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 's':
            if (sscanf(optarg, "%x", &selection_color) != 1) {
                LOG_ERR("%s: invalid color", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'B':
            if (sscanf(optarg, "%d", &border_width) != 1) {
                LOG_ERR(
                    "%s: invalid border width (must be an integer)", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'r':
            if (sscanf(optarg, "%d", &border_radius) != 1) {
                LOG_ERR("%s: invalid border radius (must be an integer)", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'C':
            if (sscanf(optarg, "%x", &border_color) != 1) {
                LOG_ERR("%s: invalid color", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'd':
            dmenu_mode = true;
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

    setlocale(LC_ALL, "");

    struct context c = {
        .status = KEEP_RUNNING,
        .exit_code = EXIT_FAILURE,
        .wl = {0},
        .dmenu_mode = dmenu_mode,
        .prompt = {
            .prompt = wcsdup(L"> "),
            .text = calloc(1, sizeof(wchar_t)),
            .cursor = 0
        },
    };

    struct fdm *fdm = NULL;

    struct font *font = font_from_name(font_name);
    if (font == NULL)
        goto out;

    LOG_DBG("height: %d, ascent: %d, descent: %d",
            font->fextents.height, font->fextents.ascent, font->fextents.descent);

    if ((fdm = fdm_init()) == NULL)
        goto out;

    if ((c.repeat = repeat_init(fdm, &repeat_cb, &c)) == NULL)
        goto out;

    if (dmenu_mode)
        dmenu_load_entries(&c.applications);
    else
        xdg_find_programs(icon_theme, font->fextents.height, &c.applications);
    c.matches = malloc(c.applications.count * sizeof(c.matches[0]));
    read_cache(&c.applications);

    const int scale = c.wl.monitor->scale;

    width /= scale; width *= scale;
    height /= scale; height *= scale;

    c.width = width;
    c.height = height;

    zwlr_layer_surface_v1_set_size(c.wl.layer_surface, width / scale, height / scale);
    zwlr_layer_surface_v1_set_keyboard_interactivity(c.wl.layer_surface, 1);

    zwlr_layer_surface_v1_add_listener(
        c.wl.layer_surface, &layer_surface_listener, &c.wl);

    /* Trigger a 'configure' event, after which we'll have the width */
    wl_surface_commit(c.wl.surface);
    wl_display_roundtrip(c.wl.display);

    const double line_height = 2 * y_margin + font->fextents.height;
    max_matches = (height - 2 * border_width - line_height) / line_height;
    LOG_DBG("max matches: %d", max_matches);

    struct options options = {
        .width = c.width,
        .height = c.height,
        .x_margin = x_margin,
        .y_margin = y_margin,
        .border_size = border_width,
        .border_radius = border_radius,
        .background_color = hex_to_rgba(background),
        .border_color = hex_to_rgba(border_color),
        .text_color = hex_to_rgba(text_color),
        .match_color = hex_to_rgba(match_color),
        .selection_color = hex_to_rgba(selection_color),
    };
    c.render = render_init(font, options);

    update_matches(&c);
    refresh(&c);

    wl_display_dispatch_pending(c.wl.display);
    wl_display_flush(c.wl.display);

    if (!fdm_add(fdm, wl_display_get_fd(c.wl.display), EPOLLIN, &fdm_wayl, &c)) {
        LOG_ERR("failed to register Wayland socket with FDM");
        goto out;
    }

    while (c.status == KEEP_RUNNING) {
        wl_display_flush(c.wl.display);
        if (!fdm_poll(fdm))
            break;
    }

    if (c.status == EXIT_UPDATE_CACHE)
        write_cache(&c.applications);

    ret = c.exit_code;

out:
    repeat_destroy(c.repeat);
    if (fdm != NULL)
        fdm_del(fdm, wl_display_get_fd(c.wl.display));
    fdm_destroy(fdm);

    render_destroy(c.render);
    shm_fini();

    free(c.prompt.prompt);
    free(c.prompt.text);
    for (size_t i = 0; i < c.applications.count; i++) {
        struct application *app = &c.applications.v[i];

        free(app->path);
        free(app->exec);
        free(app->title);
        free(app->comment);
        if (app->icon.type == ICON_SURFACE)
            cairo_surface_destroy(app->icon.surface);
        else if (app->icon.type == ICON_SVG)
            g_object_unref(app->icon.svg);
    }
    free(c.applications.v);
    free(c.matches);

    tll_foreach(c.wl.monitors, it) {
        free(it->item.name);
        if (it->item.xdg)
            zxdg_output_v1_destroy(it->item.xdg);
        if (it->item.output != NULL)
            wl_output_destroy(it->item.output);
        tll_remove(c.wl.monitors, it);
    }

    if (c.wl.xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(c.wl.xdg_output_manager);

    if (c.wl.layer_surface != NULL)
        zwlr_layer_surface_v1_destroy(c.wl.layer_surface);
    if (c.wl.layer_shell != NULL)
        zwlr_layer_shell_v1_destroy(c.wl.layer_shell);
    if (c.wl.keyboard != NULL)
        wl_keyboard_destroy(c.wl.keyboard);
    if (c.wl.surface != NULL)
        wl_surface_destroy(c.wl.surface);
    if (c.wl.seat != NULL)
        wl_seat_destroy(c.wl.seat);
    if (c.wl.compositor != NULL)
        wl_compositor_destroy(c.wl.compositor);
    if (c.wl.shm != NULL)
        wl_shm_destroy(c.wl.shm);
    if (c.wl.registry != NULL)
        wl_registry_destroy(c.wl.registry);
    if (c.wl.display != NULL)
        wl_display_disconnect(c.wl.display);
    if (c.wl.xkb_state != NULL)
        xkb_state_unref(c.wl.xkb_state);
    if (c.wl.xkb_keymap != NULL)
        xkb_keymap_unref(c.wl.xkb_keymap);
    if (c.wl.xkb != NULL)
        xkb_context_unref(c.wl.xkb);

    cairo_debug_reset_static_data();

    return ret;
}
