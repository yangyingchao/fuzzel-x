#include "wayland.h"

#include <stdlib.h>
#include <wctype.h>
#include <unistd.h>
#include <errno.h>

#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <xdg-output-unstable-v1.h>
#include <wlr-layer-shell-unstable-v1.h>

#define LOG_MODULE "wayland"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "tllist.h"
#include "prompt.h"
#include "dmenu.h"
#include "shm.h"
#include "render.h"

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

struct repeat {
    int fd;
    int32_t delay;
    int32_t rate;

    bool dont_re_repeat;
    uint32_t key;
};

struct wayland {
    struct fdm *fdm;
    struct render *render;
    struct prompt *prompt;
    struct matches *matches;

    int width;
    int height;

    struct repeat repeat;

    enum { KEEP_RUNNING, EXIT_UPDATE_CACHE, EXIT} status;
    int exit_code;
    bool dmenu_mode;

    bool frame_is_scheduled;
    struct buffer *pending;

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

static void refresh(struct wayland *wayl);

bool
repeat_start(struct repeat *repeat, uint32_t key)
{
    if (repeat->dont_re_repeat)
        return true;

    struct itimerspec t = {
        .it_value = {.tv_sec = 0, .tv_nsec = repeat->delay * 1000000},
        .it_interval = {.tv_sec = 0, .tv_nsec = 1000000000 / repeat->rate},
    };

    if (t.it_value.tv_nsec >= 1000000000) {
        t.it_value.tv_sec += t.it_value.tv_nsec / 1000000000;
        t.it_value.tv_nsec %= 1000000000;
    }
    if (t.it_interval.tv_nsec >= 1000000000) {
        t.it_interval.tv_sec += t.it_interval.tv_nsec / 1000000000;
        t.it_interval.tv_nsec %= 1000000000;
    }
    if (timerfd_settime(repeat->fd, 0, &t, NULL) < 0) {
        LOG_ERRNO("failed to arm keyboard repeat timer");
        return false;
    }

    repeat->key = key;
    return true;
}

bool
repeat_stop(struct repeat *repeat, uint32_t key)
{
    if (key != (uint32_t)-1 && key != repeat->key)
        return true;

    if (timerfd_settime(repeat->fd, 0, &(struct itimerspec){{0}}, NULL) < 0) {
        LOG_ERRNO("failed to disarm keyboard repeat timer");
        return false;
    }

    return true;
}

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
    struct wayland *wayl = data;

    char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    wayl->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    wayl->xkb_keymap = xkb_keymap_new_from_string(
        wayl->xkb, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    /* TODO: initialize in enter? */
    wayl->xkb_state = xkb_state_new(wayl->xkb_keymap);

    munmap(map_str, size);
    close(fd);
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
    struct wayland *wayl = data;
    repeat_stop(&wayl->repeat, -1);
    wayl->status = EXIT;
}

static void
keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
             uint32_t time, uint32_t key, uint32_t state)
{
    static bool mod_masks_initialized = false;
    static xkb_mod_mask_t ctrl = -1;
    static xkb_mod_mask_t alt = -1;
    //static xkb_mod_mask_t shift = -1;

    struct wayland *wayl = data;

    if (!mod_masks_initialized) {
        mod_masks_initialized = true;
        ctrl = 1 << xkb_keymap_mod_get_index(wayl->xkb_keymap, "Control");
        alt = 1 << xkb_keymap_mod_get_index(wayl->xkb_keymap, "Mod1");
        //shift = 1 << xkb_keymap_mod_get_index(wayl->xkb_keymap, "Shift");
    }

    if (state == XKB_KEY_UP) {
        repeat_stop(&wayl->repeat, key);
        return;
    }

    key += 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(wayl->xkb_state, key);

    xkb_mod_mask_t mods = xkb_state_serialize_mods(
        wayl->xkb_state, XKB_STATE_MODS_EFFECTIVE);
    xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods(wayl->xkb_state, key);
    xkb_mod_mask_t significant = ctrl | alt /*| shift*/;
    xkb_mod_mask_t effective_mods = mods & ~consumed & significant;

#if 0
    for (size_t i = 0; i < 32; i++) {
        if (mods & (1 << i)) {
            LOG_DBG("%s", xkb_keymap_mod_get_name(wayl->xkb_keymap, i));
        }
    }
#endif

    LOG_DBG("sym=%u, mod=0x%08x, consumed=0x%08x, significant=0x%08x, "
            "effective=0x%08x",
            sym, mods, consumed, significant, effective_mods);

    if (sym == XKB_KEY_Home || (sym == XKB_KEY_a && effective_mods == ctrl)) {
        if (prompt_cursor_home(wayl->prompt))
            refresh(wayl);
    }

    else if (sym == XKB_KEY_End || (sym == XKB_KEY_e && effective_mods == ctrl)) {
        if (prompt_cursor_end(wayl->prompt))
            refresh(wayl);
    }

    else if ((sym == XKB_KEY_b && effective_mods == alt) ||
             (sym == XKB_KEY_Left && effective_mods == ctrl)) {
        if (prompt_cursor_prev_word(wayl->prompt))
            refresh(wayl);
    }

    else if ((sym == XKB_KEY_f && effective_mods == alt) ||
             (sym == XKB_KEY_Right && effective_mods == ctrl)) {

        if (prompt_cursor_next_word(wayl->prompt))
            refresh(wayl);
    }

    else if ((sym == XKB_KEY_Escape && effective_mods == 0) ||
             (sym == XKB_KEY_g && effective_mods == ctrl)) {
        wayl->status = EXIT;
    }

    else if ((sym == XKB_KEY_p && effective_mods == ctrl) ||
             (sym == XKB_KEY_Up && effective_mods == 0)) {
        if (matches_selected_prev(wayl->matches, false))
            refresh(wayl);
    }

    else if ((sym == XKB_KEY_n && effective_mods == ctrl) ||
             (sym == XKB_KEY_Down && effective_mods == 0)) {
        if (matches_selected_next(wayl->matches, false))
            refresh(wayl);
    }

    else if (sym == XKB_KEY_Tab && effective_mods == 0) {
        if (matches_selected_next(wayl->matches, true))
            refresh(wayl);
    }

    else if (sym == XKB_KEY_ISO_Left_Tab && effective_mods == 0) {
        if (matches_selected_prev(wayl->matches, true))
            refresh(wayl);
    }

    else if ((sym == XKB_KEY_b && effective_mods == ctrl) ||
             (sym == XKB_KEY_Left && effective_mods == 0)) {
        if (prompt_cursor_prev_char(wayl->prompt))
            refresh(wayl);
    }

    else if ((sym == XKB_KEY_f && effective_mods == ctrl) ||
             (sym == XKB_KEY_Right && effective_mods == 0)) {
        if (prompt_cursor_next_char(wayl->prompt))
            refresh(wayl);
    }

    else if ((sym == XKB_KEY_d && effective_mods == ctrl) ||
             (sym == XKB_KEY_Delete && effective_mods == 0)) {
        if (prompt_erase_next_char(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            refresh(wayl);
        }
    }

    else if (sym == XKB_KEY_BackSpace && effective_mods == 0) {
        if (prompt_erase_prev_char(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            refresh(wayl);
        }
    }

    else if (sym == XKB_KEY_BackSpace && (effective_mods == ctrl ||
                                          effective_mods == alt)) {
        if (prompt_erase_prev_word(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            refresh(wayl);
        }
    }

    else if ((sym == XKB_KEY_d && effective_mods == alt) ||
             (sym == XKB_KEY_Delete && effective_mods == ctrl)) {
        if (prompt_erase_next_word(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            refresh(wayl);
        }
    }

    else if (sym == XKB_KEY_k && effective_mods == ctrl) {
        if (prompt_erase_after_cursor(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            refresh(wayl);
        }
    }

    else if (sym == XKB_KEY_Return && effective_mods == 0) {
        wayl->status = EXIT;

        const struct match *match = matches_get_match(wayl->matches);
        struct application *app = match != NULL ? match->application : NULL;

        if (wayl->dmenu_mode) {
            if (match == NULL) {
                wayl->status = KEEP_RUNNING;
            } else {
                dmenu_execute(app);
                wayl->exit_code = EXIT_SUCCESS;
            }
        } else {
            bool success = application_execute(app, wayl->prompt);
            wayl->exit_code = success ? EXIT_SUCCESS : EXIT_FAILURE;

            if (success && match != NULL) {
                wayl->status = EXIT_UPDATE_CACHE;
                app->count++;
            }
        }
    }

    else if (effective_mods == 0) {
        char buf[128] = {0};
        int count = xkb_state_key_get_utf8(wayl->xkb_state, key, buf, sizeof(buf));

        if (count == 0)
            return;

        if (!prompt_insert_chars(wayl->prompt, buf, count))
            return;

        matches_update(wayl->matches, wayl->prompt);
        refresh(wayl);
    }

    repeat_start(&wayl->repeat, key - 8);

}

static void
keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                   uint32_t mods_depressed, uint32_t mods_latched,
                   uint32_t mods_locked, uint32_t group)
{
    struct wayland *wayl = data;

    LOG_DBG("modifiers: depressed=0x%x, latched=0x%x, locked=0x%x, group=%u",
            mods_depressed, mods_latched, mods_locked, group);

    xkb_state_update_mask(
        wayl->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                     int32_t rate, int32_t delay)
{
    struct wayland *wayl = data;
    LOG_DBG("keyboard repeat: rate=%d, delay=%d", rate, delay);
    wayl->repeat.delay = delay;
    wayl->repeat.rate = rate;
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
    struct wayland *wayl = data;

    if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD))
        return;

    if (wayl->keyboard != NULL)
        wl_keyboard_release(wayl->keyboard);

    wayl->keyboard = wl_seat_get_keyboard(wl_seat);
    wl_keyboard_add_listener(wayl->keyboard, &keyboard_listener, wayl);
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
    mon->width_mm = physical_width;
    mon->height_mm = physical_height;
}

static void
output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
            int32_t width, int32_t height, int32_t refresh)
{
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
xdg_output_handle_logical_position(
    void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y)
{
    struct monitor *mon = data;
    mon->x = x;
    mon->y = y;
}

static void
xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
                               int32_t width, int32_t height)
{
    struct monitor *mon = data;
    mon->width_px = width;
    mon->height_px = height;
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

static bool
verify_iface_version(const char *iface, uint32_t version, uint32_t wanted)
{
    if (version >= wanted)
        return true;

    LOG_ERR("%s: need interface version %u, but compositor only implements %u",
            iface, wanted, version);
    return false;
}

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    struct wayland *wayl = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        const uint32_t required = 4;
        if (!verify_iface_version(interface, version, required))
            return;

        wayl->compositor = wl_registry_bind(
            wayl->registry, name, &wl_compositor_interface, required);
    }

    else if (strcmp(interface, wl_shm_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        wayl->shm = wl_registry_bind(
            wayl->registry, name, &wl_shm_interface, required);
        wl_shm_add_listener(wayl->shm, &shm_listener, wayl);
        wl_display_roundtrip(wayl->display);
    }

    else if (strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t required = 3;
        if (!verify_iface_version(interface, version, required))
            return;

        struct wl_output *output = wl_registry_bind(
            wayl->registry, name, &wl_output_interface, required);

        tll_push_back(wayl->monitors, ((struct monitor){.output = output}));

        struct monitor *mon = &tll_back(wayl->monitors);
        wl_output_add_listener(output, &output_listener, mon);

        /*
         * The "output" interface doesn't give us the monitors'
         * identifiers (e.g. "LVDS-1"). Use the XDG output interface
         * for that.
         */

        assert(wayl->xdg_output_manager != NULL);
        if (wayl->xdg_output_manager != NULL) {
            mon->xdg = zxdg_output_manager_v1_get_xdg_output(
                wayl->xdg_output_manager, mon->output);

            zxdg_output_v1_add_listener(mon->xdg, &xdg_output_listener, mon);
        }
        wl_display_roundtrip(wayl->display);
    }

    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        wayl->layer_shell = wl_registry_bind(
            wayl->registry, name, &zwlr_layer_shell_v1_interface, required);
    }

    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        const uint32_t required = 4;
        if (!verify_iface_version(interface, version, required))
            return;

        wayl->seat = wl_registry_bind(
            wayl->registry, name, &wl_seat_interface, required);
        wl_seat_add_listener(wayl->seat, &seat_listener, wayl);
        wl_display_roundtrip(wayl->display);
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        const uint32_t required = 2;
        if (!verify_iface_version(interface, version, required))
            return;

        wayl->xdg_output_manager = wl_registry_bind(
            wayl->registry, name, &zxdg_output_manager_v1_interface, required);
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

static void frame_callback(
    void *data, struct wl_callback *wl_callback, uint32_t callback_data);

static const struct wl_callback_listener frame_listener = {
    .done = &frame_callback,
};

static void
commit_buffer(struct wayland *wayl, struct buffer *buf)
{
    assert(buf->busy);

    wl_surface_set_buffer_scale(wayl->surface, wayl->monitor->scale);
    wl_surface_attach(wayl->surface, buf->wl_buf, 0, 0);
    wl_surface_damage_buffer(wayl->surface, 0, 0, buf->width, buf->height);

    struct wl_callback *cb = wl_surface_frame(wayl->surface);
    wl_callback_add_listener(cb, &frame_listener, wayl);

    wayl->frame_is_scheduled = true;
    wl_surface_commit(wayl->surface);
}

static void
frame_callback(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
    struct wayland *wayl = data;

    wayl->frame_is_scheduled = false;
    wl_callback_destroy(wl_callback);

    if (wayl->pending != NULL) {
        commit_buffer(wayl, wayl->pending);
        wayl->pending = NULL;
    }
}

static void
refresh(struct wayland *wayl)
{
    struct buffer *buf = shm_get_buffer(wayl->shm, wayl->width, wayl->height);

    /* Background + border */
    render_background(wayl->render, buf);

    /* Window content */
    render_prompt(wayl->render, buf, wayl->prompt);
    render_match_list(wayl->render, buf, wayl->prompt, wayl->matches);

    cairo_surface_flush(buf->cairo_surface);

    if (wayl->frame_is_scheduled) {
        /* There's already a frame being drawn - delay current frame
         * (overwriting any previous pending frame) */

        if (wayl->pending != NULL)
            wayl->pending->busy = false;

        wayl->pending = buf;
    } else {
        /* No pending frames - render immediately */
        assert(wayl->pending == NULL);
        commit_buffer(wayl, buf);
    }
}

static bool
fdm_handler(struct fdm *fdm, int fd, int events, void *data)
{
    struct wayland *wayl = data;
    int event_count = wl_display_dispatch(wayl->display);

    if (events & EPOLLHUP) {
        LOG_WARN("disconnected from Wayland");
        return false;
    }

    return event_count != -1 && wayl->status == KEEP_RUNNING;
}

static bool
fdm_repeat(struct fdm *fdm, int fd, int events, void *data)
{
    struct wayland *wayl = data;
    struct repeat *repeat = &wayl->repeat;

    uint64_t expiration_count;
    ssize_t ret = read(
        repeat->fd, &expiration_count, sizeof(expiration_count));

    if (ret < 0) {
        if (errno == EAGAIN)
            return true;

        LOG_ERRNO("failed to read key repeat count from timer fd");
        return false;
    }

    repeat->dont_re_repeat = true;
    for (size_t i = 0; i < expiration_count; i++)
        keyboard_key(wayl, NULL, 0, 0, repeat->key, XKB_KEY_DOWN);
    repeat->dont_re_repeat = false;

    if (events & EPOLLHUP) {
        LOG_ERR("keyboard repeater timer FD closed unexpectedly");
        return false;
    }

    return true;
}

struct wayland *
wayl_init(struct fdm *fdm, struct render *render, struct prompt *prompt,
          struct matches *matches, int width, int height,
          const char *output_name, bool dmenu_mode)
{
    struct wayland *wayl = calloc(1, sizeof(*wayl));

    wayl->fdm = fdm;
    wayl->render = render;
    wayl->prompt = prompt;
    wayl->matches = matches;
    wayl->status = KEEP_RUNNING;
    wayl->exit_code = EXIT_FAILURE;
    wayl->dmenu_mode = dmenu_mode;

    wayl->repeat.fd = timerfd_create(CLOCK_BOOTTIME, TFD_CLOEXEC | TFD_NONBLOCK);
    if (wayl->repeat.fd == -1) {
        LOG_ERRNO("failed to create keyboard repeat timer FD");
        goto out;
    }

    wayl->display = wl_display_connect(NULL);
    if (wayl->display == NULL) {
        LOG_ERR("failed to connect to wayland; no compositor running?");
        goto out;
    }

    wayl->registry = wl_display_get_registry(wayl->display);
    if (wayl->registry == NULL) {
        LOG_ERR("failed to get wayland registry");
        goto out;
    }

    wl_registry_add_listener(wayl->registry, &registry_listener, wayl);
    wl_display_roundtrip(wayl->display);

    if (wayl->compositor == NULL) {
        LOG_ERR("no compositor");
        goto out;
    }
    if (wayl->layer_shell == NULL) {
        LOG_ERR("no layer shell interface");
        goto out;
    }
    if (wayl->shm == NULL) {
        LOG_ERR("no shared memory buffers interface");
        goto out;
    }

    if (tll_length(wayl->monitors) == 0) {
        LOG_ERR("no monitors");
        goto out;
    }

    tll_foreach(wayl->monitors, it) {
        const struct monitor *mon = &it->item;
        LOG_INFO("monitor: %s: %dx%d+%d+%d (%dx%dmm)",
                 mon->name, mon->width_px, mon->height_px,
                 mon->x, mon->y, mon->width_mm, mon->height_mm);

        if (output_name != NULL && strcmp(output_name, mon->name) == 0) {
            wayl->monitor = mon;
            break;
        } else if (output_name == NULL) {
            /* Use last monitor found */
            wayl->monitor = mon;
        }
    }

    if (wayl->monitor == NULL && output_name != NULL) {
        LOG_ERR("%s: no output with that name found", output_name);
        goto out;
    }

    if (wayl->monitor == NULL) {
        LOG_ERR("no outputs found");
        goto out;
    }

    wayl->surface = wl_compositor_create_surface(wayl->compositor);
    if (wayl->surface == NULL) {
        LOG_ERR("failed to create panel surface");
        goto out;
    }

#if 0
    wayl->pointer.surface = wl_compositor_create_surface(wayl->compositor);
    if (wayl->pointer.surface == NULL) {
        LOG_ERR("failed to create cursor surface");
        goto out;
    }

    wayl->pointer.theme = wl_cursor_theme_load(NULL, 24, wayl->shm);
    if (wayl->pointer.theme == NULL) {
        LOG_ERR("failed to load cursor theme");
        goto out;
    }
#endif

    wayl->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        wayl->layer_shell, wayl->surface, wayl->monitor->output,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "fuzzel");

    if (wayl->layer_surface == NULL) {
        LOG_ERR("failed to create layer shell surface");
        goto out;
    }

    const int scale = wayl->monitor->scale;

    width /= scale; width *= scale;
    height /= scale; height *= scale;

    wayl->width = width;
    wayl->height = height;

    zwlr_layer_surface_v1_set_size(wayl->layer_surface, width / scale, height / scale);
    zwlr_layer_surface_v1_set_keyboard_interactivity(wayl->layer_surface, 1);

    zwlr_layer_surface_v1_add_listener(
        wayl->layer_surface, &layer_surface_listener, wayl);

    /* Trigger a 'configure' event, after which we'll have the width */
    wl_surface_commit(wayl->surface);
    wl_display_roundtrip(wayl->display);

    if (!fdm_add(wayl->fdm, wl_display_get_fd(wayl->display), EPOLLIN, &fdm_handler, wayl)) {
        LOG_ERR("failed to register Wayland socket with FDM");
        goto out;
    }

    if (!fdm_add(wayl->fdm, wayl->repeat.fd, EPOLLIN, &fdm_repeat, wayl)) {
        LOG_ERR("failed to register keyboard repeat timer FD with FDM");
        goto out;
    }

    refresh(wayl);
    return wayl;

out:
    wayl_destroy(wayl);
    return NULL;
}

void
wayl_destroy(struct wayland *wayl)
{
    if (wayl == NULL)
        return;

    if (wayl->display != NULL)
        fdm_del_no_close(wayl->fdm, wl_display_get_fd(wayl->display));

    if (wayl->repeat.fd > 0)
        fdm_del(wayl->fdm, wayl->repeat.fd);

    tll_foreach(wayl->monitors, it) {
        free(it->item.name);
        if (it->item.xdg)
            zxdg_output_v1_destroy(it->item.xdg);
        if (it->item.output != NULL)
            wl_output_destroy(it->item.output);
        tll_remove(wayl->monitors, it);
    }

    if (wayl->xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(wayl->xdg_output_manager);

    if (wayl->layer_surface != NULL)
        zwlr_layer_surface_v1_destroy(wayl->layer_surface);
    if (wayl->layer_shell != NULL)
        zwlr_layer_shell_v1_destroy(wayl->layer_shell);
    if (wayl->keyboard != NULL)
        wl_keyboard_destroy(wayl->keyboard);
    if (wayl->surface != NULL)
        wl_surface_destroy(wayl->surface);
    if (wayl->seat != NULL)
        wl_seat_destroy(wayl->seat);
    if (wayl->compositor != NULL)
        wl_compositor_destroy(wayl->compositor);
    if (wayl->shm != NULL)
        wl_shm_destroy(wayl->shm);
    if (wayl->registry != NULL)
        wl_registry_destroy(wayl->registry);
    if (wayl->display != NULL)
        wl_display_disconnect(wayl->display);
    if (wayl->xkb_state != NULL)
        xkb_state_unref(wayl->xkb_state);
    if (wayl->xkb_keymap != NULL)
        xkb_keymap_unref(wayl->xkb_keymap);
    if (wayl->xkb != NULL)
        xkb_context_unref(wayl->xkb);

    free(wayl);
}

void
wayl_flush(struct wayland *wayl)
{
    wl_display_flush(wayl->display);
}

bool
wayl_exit_code(const struct wayland *wayl)
{
    return wayl->exit_code;
}

bool
wayl_update_cache(const struct wayland *wayl)
{
    return wayl->status == EXIT_UPDATE_CACHE;
}
