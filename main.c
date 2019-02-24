#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <poll.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <dirent.h>

#include <cairo.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>


#include <xdg-output-unstable-v1.h>
#include <wlr-layer-shell-unstable-v1.h>

#define LOG_MODULE "f00sel"
#define LOG_ENABLE_DBG 1
#include "log.h"
#include "tllist.h"
#include "shm.h"
#include "font.h"
#include "render.h"
#include "application.h"
#include "match.h"
#include "xdg.h"

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

static const int width = 500;
static const int height = 300;

static double font_size;
static cairo_scaled_font_t *scaled_font;

static struct prompt prompt;
static application_list_t applications;
static struct match *matches;
static size_t match_count;
static size_t selected;

static bool keep_running = true;

static void refresh(const struct wayland *wl);
static void update_matches(void);

static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
}

static const struct wl_shm_listener shm_listener = {
    .format = &shm_format,
};

static void
keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                uint32_t format, int32_t fd, uint32_t size)
{
    struct wayland *wl = data;

    char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    wl->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    wl->xkb_keymap = xkb_keymap_new_from_string(
        wl->xkb, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    /* TODO: initialize in enter? */
    wl->xkb_state = xkb_state_new(wl->xkb_keymap);

    munmap(map_str, size);
    /* TODO: should we close(fd)? */
}

static void
keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
               struct wl_surface *surface, struct wl_array *keys)
{
    LOG_DBG("enter");
#if 0
    uint32_t *key;
    wl_array_for_each(key, keys)
        xkb_state_update_key(xkb_state, *key, 1);
#endif
}

static void
keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
               struct wl_surface *surface)
{
    keep_running = false;
}

static size_t
prompt_prev_word(void)
{
    const char *space = &prompt.text[prompt.cursor - 1];

    /* Ignore initial spaces */
    while (space >= prompt.text && *space == ' ')
        space--;

    while (space >= prompt.text && *space != ' ')
        space--;

    return space - prompt.text + 1;
}

static size_t
prompt_next_word(void)
{
    const char *space = strchr(&prompt.text[prompt.cursor], ' ');
    if (space == NULL)
        return strlen(prompt.text);
    else
        return space - prompt.text + 1;
}

static void
keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
             uint32_t time, uint32_t key, uint32_t state)
{
    static bool mod_masks_initialized = false;
    static xkb_mod_mask_t ctrl = -1;
    static xkb_mod_mask_t alt = -1;
    //static xkb_mod_mask_t shift = -1;

    struct wayland *wl = data;

    if (!mod_masks_initialized) {
        mod_masks_initialized = true;
        ctrl = 1 << xkb_keymap_mod_get_index(wl->xkb_keymap, "Control");
        alt = 1 << xkb_keymap_mod_get_index(wl->xkb_keymap, "Mod1");
        //shift = 1 << xkb_keymap_mod_get_index(wl->xkb_keymap, "Shift");
    }

    if (state == XKB_KEY_UP)
        return;

    key += 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(wl->xkb_state, key);

    xkb_mod_mask_t mods = xkb_state_serialize_mods(
        wl->xkb_state, XKB_STATE_MODS_EFFECTIVE);
    xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods(wl->xkb_state, key);
    xkb_mod_mask_t significant = ctrl | alt /*| shift*/;
    xkb_mod_mask_t effective_mods = mods & ~consumed & significant;

#if 0
    for (size_t i = 0; i < 32; i++) {
        if (mods & (1 << i)) {
            LOG_DBG("%s", xkb_keymap_mod_get_name(wl->xkb_keymap, i));
        }
    }
#endif

    LOG_DBG("mod=0x%08x, consumed=0x%08x, significant=0x%08x, effective=0x%08x",
            mods, consumed, significant, effective_mods);

    if (sym == XKB_KEY_Home || (sym == XKB_KEY_a && effective_mods == ctrl)) {
        prompt.cursor = 0;
        refresh(wl);
    }

    else if (sym == XKB_KEY_End || (sym == XKB_KEY_e && effective_mods == ctrl)) {
        prompt.cursor = strlen(prompt.text);
        refresh(wl);
    }

    else if ((sym == XKB_KEY_b && effective_mods == alt) ||
             (sym == XKB_KEY_Left && effective_mods == ctrl)) {
        prompt.cursor = prompt_prev_word();
        refresh(wl);
    }

    else if ((sym == XKB_KEY_f && effective_mods == alt) ||
             (sym == XKB_KEY_Right && effective_mods == ctrl)) {

        prompt.cursor = prompt_next_word();
        refresh(wl);
    }

    else if (sym == XKB_KEY_Escape && effective_mods == 0)
        keep_running = false;

    else if ((sym == XKB_KEY_p && effective_mods == ctrl) ||
             (sym == XKB_KEY_Up && effective_mods == 0)) {
        if (selected > 0) {
            selected--;
            refresh(wl);
        }
    }

    else if ((sym == XKB_KEY_n && effective_mods == ctrl) ||
             (sym == XKB_KEY_Down && effective_mods == 0)) {
        if (selected + 1 < match_count) {
            selected++;
            refresh(wl);
        }
    }

    else if (sym == XKB_KEY_Tab && effective_mods == 0) {
        if (selected + 1 < match_count) {
            selected++;
            refresh(wl);
        } else if (sym == XKB_KEY_Tab) {
            /* Tab cycles */
            selected = 0;
            refresh(wl);
        }
    }

    else if ((sym == XKB_KEY_b && effective_mods == ctrl) ||
             (sym == XKB_KEY_Left && effective_mods == 0)) {
        if (prompt.cursor > 0) {
            prompt.cursor--;
            refresh(wl);
        }
    }

    else if ((sym == XKB_KEY_f && effective_mods == ctrl) ||
             (sym == XKB_KEY_Right && effective_mods == 0)) {
        if (prompt.cursor + 1 <= strlen(prompt.text)) {
            prompt.cursor++;
            refresh(wl);
        }
    }

    else if ((sym == XKB_KEY_d && effective_mods == ctrl) ||
             (sym == XKB_KEY_Delete && effective_mods == 0)) {
        if (prompt.cursor < strlen(prompt.text)) {
            memmove(&prompt.text[prompt.cursor], &prompt.text[prompt.cursor + 1],
                    strlen(prompt.text) - prompt.cursor);
            update_matches();
            refresh(wl);
        }
    }

    else if (sym == XKB_KEY_BackSpace && effective_mods == 0) {
        if (prompt.cursor > 0) {
            prompt.text[strlen(prompt.text) - 1] = '\0';
            prompt.cursor--;

            update_matches();
            refresh(wl);
        }
    }

    else if (sym == XKB_KEY_BackSpace && (effective_mods == ctrl ||
                                          effective_mods == alt)) {
        size_t new_cursor = prompt_prev_word();
        memmove(&prompt.text[new_cursor],
                &prompt.text[prompt.cursor],
                strlen(prompt.text) - prompt.cursor + 1);
        prompt.cursor = new_cursor;
        update_matches();
        refresh(wl);
    }

    else if ((sym == XKB_KEY_d && effective_mods == alt) ||
             (sym == XKB_KEY_Delete && effective_mods == ctrl)) {
        size_t next_word = prompt_next_word();
        memmove(&prompt.text[prompt.cursor],
                &prompt.text[next_word],
                strlen(prompt.text) - next_word + 1);
        update_matches();
        refresh(wl);
    }

    else if (sym == XKB_KEY_Return && effective_mods == 0) {
        if (match_count == 0)
            return;

        assert(selected < match_count);
        LOG_DBG("exec(%s)", matches[selected].application->path);

        pid_t pid = fork();
        if (pid == -1)
            LOG_ERRNO("failed to fork");

        if (pid == 0) {
            /* Child */
            pid = fork();
            if (pid == 0)
                execlp(matches[selected].application->path,
                       matches[selected].application->path, NULL);
            keep_running = false;
        } else {
            /* Parent */
            int status;
            if (waitpid(pid, &status, 0) == -1)
                LOG_ERRNO("failed to wait for pid %u", pid);

            keep_running = false;
        }
    }

    else if (effective_mods == 0) {
        char buf[128] = {0};
        int count = xkb_state_key_get_utf8(wl->xkb_state, key, buf, sizeof(buf));

        if (count == 0)
            return;

        const size_t new_len = strlen(prompt.text) + count + 1;
        char *new_text = malloc(new_len);

        /* Everything from old prompt, up to the cursor */
        memcpy(new_text, prompt.text, prompt.cursor);
        new_text[prompt.cursor] = '\0';

        /* New text just entered */
        strcat(new_text, buf);

        /* Everything from old prompt, after cursor */
        strcat(new_text, &prompt.text[prompt.cursor]);
        new_text[new_len - 1] = '\0';

        free(prompt.text);
        prompt.text = new_text;
        prompt.cursor += count;

        LOG_DBG("prompt: \"%s\" (cursor=%zu, length=%zu)",
                prompt.text, prompt.cursor, new_len);

        update_matches();
        refresh(wl);
    }
}

static void
keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                   uint32_t mods_depressed, uint32_t mods_latched,
                   uint32_t mods_locked, uint32_t group)
{
    struct wayland *wl = data;

    LOG_DBG("modifiers: depressed=0x%x, latched=0x%x, locked=0x%x, group=%u",
            mods_depressed, mods_latched, mods_locked, group);

    xkb_state_update_mask(
        wl->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                     int32_t rate, int32_t delay)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = &keyboard_keymap,
    .enter = &keyboard_enter,
    .leave = &keyboard_leave,
    .key = &keyboard_key,
    .modifiers = &keyboard_modifiers,
    .repeat_info = &keyboard_repeat_info,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                         enum wl_seat_capability caps)
{
    struct wayland *wl = data;

    if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD))
        return;

    if (wl->keyboard != NULL)
        wl_keyboard_release(wl->keyboard);
    wl->keyboard = wl_seat_get_keyboard(wl_seat);
    wl_keyboard_add_listener(wl->keyboard, &keyboard_listener, wl);
}

static void
seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void
output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                int32_t physical_width, int32_t physical_height,
                int32_t subpixel, const char *make, const char *model,
                int32_t transform)
{
    struct monitor *mon = data;
    mon->x = x;
    mon->y = y;
    mon->width_mm = physical_width;
    mon->height_mm = physical_height;
}

static void
output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
            int32_t width, int32_t height, int32_t refresh)
{
    struct monitor *mon = data;
    mon->width_px = width;
    mon->height_px = height;
}

static void
output_done(void *data, struct wl_output *wl_output)
{
}

static void
output_scale(void *data, struct wl_output *wl_output, int32_t factor)
{
    struct monitor *mon = data;
    mon->scale = factor;
}


static const struct wl_output_listener output_listener = {
    .geometry = &output_geometry,
    .mode = &output_mode,
    .done = &output_done,
    .scale = &output_scale,
};

static void
xdg_output_handle_logical_position(void *data,
                                   struct zxdg_output_v1 *xdg_output,
                                   int32_t x, int32_t y)
{
}

static void
xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
                               int32_t width, int32_t height)
{
}

static void
xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output)
{
}

static void
xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output,
                       const char *name)
{
    struct monitor *mon = data;
    mon->name = strdup(name);
}

static void
xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output,
                              const char *description)
{
}

static struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    struct wayland *wl = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wl->compositor = wl_registry_bind(
            wl->registry, name, &wl_compositor_interface, 4);
    }

    else if (strcmp(interface, wl_shm_interface.name) == 0) {
        wl->shm = wl_registry_bind(wl->registry, name, &wl_shm_interface, 1);
        wl_shm_add_listener(wl->shm, &shm_listener, wl);
        wl_display_roundtrip(wl->display);
    }

    else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output = wl_registry_bind(
            wl->registry, name, &wl_output_interface, 3);

        tll_push_back(wl->monitors, ((struct monitor){.output = output}));

        struct monitor *mon = &tll_back(wl->monitors);
        wl_output_add_listener(output, &output_listener, mon);

        /*
         * The "output" interface doesn't give us the monitors'
         * identifiers (e.g. "LVDS-1"). Use the XDG output interface
         * for that.
         */

        assert(wl->xdg_output_manager != NULL);
        mon->xdg = zxdg_output_manager_v1_get_xdg_output(
            wl->xdg_output_manager, mon->output);

        zxdg_output_v1_add_listener(mon->xdg, &xdg_output_listener, mon);
        wl_display_roundtrip(wl->display);
    }

    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        wl->layer_shell = wl_registry_bind(
            wl->registry, name, &zwlr_layer_shell_v1_interface, 1);
    }

    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        wl->seat = wl_registry_bind(wl->registry, name, &wl_seat_interface, 3);
        wl_seat_add_listener(wl->seat, &seat_listener, wl);
        wl_display_roundtrip(wl->display);
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        wl->xdg_output_manager = wl_registry_bind(
            wl->registry, name, &zxdg_output_manager_v1_interface, 2);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    LOG_WARN("global removed: %u", name);
}

static const struct wl_registry_listener registry_listener = {
    .global = &handle_global,
    .global_remove = &handle_global_remove,
};

static void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                        uint32_t serial, uint32_t w, uint32_t h)
{
    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
};

#if 0
static void
find_programs(void)
{
    DIR *d = opendir("/usr/bin");
    if (d == NULL)
        return;

    for (struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        int len = snprintf(NULL, 0, "/usr/bin/%s", e->d_name);
        char *path = malloc(len + 1);
        snprintf(path, len + 1, "/usr/bin/%s", e->d_name);

        struct application app = {
            .title = strdup(e->d_name),
            .path = path,
        };

        tll_push_back(applications, app);
    }

    closedir(d);
}
#endif

static void
update_matches(void)
{
    /* Nothing entered; all programs found matches */
    if (strlen(prompt.text) == 0) {
        match_count = tll_length(applications);

        size_t i = 0;
        tll_foreach(applications, it)
            matches[i++] = (struct match){.application = &it->item, .start = 0};

        assert(selected < match_count);
        return;
    }

    tll(struct match) _matches = tll_init();
    tll_foreach(applications, it) {
        const char *m = strcasestr(it->item.title, prompt.text);
        if (m == NULL)
            continue;

        const size_t start = m - it->item.title;
        tll_push_back(
            _matches,
            ((struct match){.application = &it->item, .start = start}));
    }

    size_t i = 0;
    match_count = tll_length(_matches);
    tll_foreach(_matches, it)
        matches[i++] = it->item;

    tll_free(_matches);

    if (selected >= match_count && selected > 0)
        selected = match_count - 1;
}

static void
refresh(const struct wayland *wl)
{
    struct buffer *buf = shm_get_buffer(wl->shm, width, height);

    /* Background */
    cairo_set_source_rgba(buf->cairo, 0.067, 0.067, 0.067, 0.9);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
    cairo_paint(buf->cairo);

    /* Border */
    cairo_set_source_rgba(buf->cairo, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width(buf->cairo, 2);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);
    cairo_rectangle(buf->cairo, 0, 0, width, height);
    cairo_stroke(buf->cairo);

    render_prompt(buf, scaled_font, font_size, &prompt);
    render_match_list(buf, scaled_font, font_size, matches, match_count, selected);

    cairo_surface_flush(buf->cairo_surface);
    wl_surface_attach(wl->surface, buf->wl_buf, 0, 0);
    wl_surface_damage(wl->surface, 0, 0, buf->width, buf->height);
    wl_surface_commit(wl->surface);
}

int
main(int argc, const char *const *argv)
{
    prompt.text = calloc(1, 1);
    prompt.cursor = 0;

    //find_programs();
    xdg_find_programs(&applications);
    matches = malloc(tll_length(applications) * sizeof(matches[0]));
    update_matches();

    struct wayland wl = {0};

    wl.display = wl_display_connect(NULL);
    if (wl.display == NULL) {
        LOG_ERR("failed to connect to wayland; no compositor running?");
        goto out;
    }

    wl.registry = wl_display_get_registry(wl.display);
    if (wl.registry == NULL) {
        LOG_ERR("failed to get wayland registry");
        goto out;
    }

    wl_registry_add_listener(wl.registry, &registry_listener, &wl);
    wl_display_roundtrip(wl.display);

    if (wl.compositor == NULL) {
        LOG_ERR("no compositor");
        goto out;
    }
    if (wl.layer_shell == NULL) {
        LOG_ERR("no layer shell interface");
        goto out;
    }
    if (wl.shm == NULL) {
        LOG_ERR("no shared memory buffers interface");
        goto out;
    }

    if (tll_length(wl.monitors) == 0) {
        LOG_ERR("no monitors");
        goto out;
    }

    tll_foreach(wl.monitors, it) {
        const struct monitor *mon = &it->item;
        LOG_INFO("monitor: %s: %dx%d+%d+%d (%dx%dmm)",
                 mon->name, mon->width_px, mon->height_px,
                 mon->x, mon->y, mon->width_mm, mon->height_mm);

#if 0
        /* TODO: detect primary output when user hasn't specified a monitor */
        if (bar->monitor == NULL)
            monitor = mon;
        else if (strcmp(bar->monitor, mon->name) == 0)
            monitor = mon;
#endif
        wl.monitor = mon;
        break;
    }

    assert(wl.monitor != NULL);

    wl.surface = wl_compositor_create_surface(wl.compositor);
    if (wl.surface == NULL) {
        LOG_ERR("failed to create panel surface");
        goto out;
    }

#if 0
    wl.pointer.surface = wl_compositor_create_surface(wl.compositor);
    if (wl.pointer.surface == NULL) {
        LOG_ERR("failed to create cursor surface");
        goto out;
    }

    wl.pointer.theme = wl_cursor_theme_load(NULL, 24, wl.shm);
    if (wl.pointer.theme == NULL) {
        LOG_ERR("failed to load cursor theme");
        goto out;
    }
#endif

    wl.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        wl.layer_shell, wl.surface, wl.monitor->output,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "f00sel");

    if (wl.layer_surface == NULL) {
        LOG_ERR("failed to create layer shell surface");
        goto out;
    }

    zwlr_layer_surface_v1_set_size(wl.layer_surface, width, height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(wl.layer_surface, 1);

    zwlr_layer_surface_v1_add_listener(
        wl.layer_surface, &layer_surface_listener, &wl);

    /* Trigger a 'configure' event, after which we'll have the width */
    wl_surface_commit(wl.surface);
    wl_display_roundtrip(wl.display);

    const char *font_name = "Dina:pixelsize=9";
    scaled_font = font_from_name(font_name, &font_size);
    refresh(&wl);

    wl_display_dispatch_pending(wl.display);
    wl_display_flush(wl.display);

    while (keep_running) {
        struct pollfd fds[] = {
            {.fd = wl_display_get_fd(wl.display), .events = POLLIN},
        };

        wl_display_flush(wl.display);

        if (fds[0].revents & POLLHUP) {
            LOG_WARN("disconnected from wayland");
            break;
        }

        wl_display_dispatch(wl.display);
    }

out:
    shm_fini();

    free(matches);
    tll_foreach(applications, it) {
        free(it->item.title);
        free(it->item.path);
        tll_remove(applications, it);
    }

    tll_foreach(wl.monitors, it) {
        free(it->item.name);
        if (it->item.xdg)
            zxdg_output_v1_destroy(it->item.xdg);
        if (it->item.output != NULL)
            wl_output_destroy(it->item.output);
        tll_remove(wl.monitors, it);
    }

    if (wl.xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(wl.xdg_output_manager);

    if (wl.layer_surface != NULL)
        zwlr_layer_surface_v1_destroy(wl.layer_surface);
    if (wl.layer_shell != NULL)
        zwlr_layer_shell_v1_destroy(wl.layer_shell);
    if (wl.keyboard != NULL)
        wl_keyboard_destroy(wl.keyboard);
    if (wl.surface != NULL)
        wl_surface_destroy(wl.surface);
    if (wl.seat != NULL)
        wl_seat_destroy(wl.seat);
    if (wl.compositor != NULL)
        wl_compositor_destroy(wl.compositor);
    if (wl.shm != NULL)
        wl_shm_destroy(wl.shm);
    if (wl.registry != NULL)
        wl_registry_destroy(wl.registry);
    if (wl.display != NULL)
        wl_display_disconnect(wl.display);
    if (wl.xkb_state != NULL)
        xkb_state_unref(wl.xkb_state);
    if (wl.xkb_keymap != NULL)
        xkb_keymap_unref(wl.xkb_keymap);
    if (wl.xkb != NULL)
        xkb_context_unref(wl.xkb);

    return 0;
}
