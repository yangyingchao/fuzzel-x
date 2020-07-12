#include "wayland.h"

#include <stdlib.h>
#include <wctype.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>

#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-compose.h>

#include <xdg-output-unstable-v1.h>
#include <wlr-layer-shell-unstable-v1.h>

#include <tllist.h>

#define LOG_MODULE "wayland"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "dmenu.h"
#include "icon.h"
#include "prompt.h"
#include "render.h"
#include "shm.h"

struct monitor {
    struct wayland *wayl;
    struct wl_output *output;
    struct zxdg_output_v1 *xdg;
    uint32_t wl_name;

    int x;
    int y;

    struct {
        /* Physical size, in mm */
        struct {
            int width;
            int height;
        } mm;

        /* Physical size, in pixels */
        struct {
            int width;
            int height;
        } px_real;

        /* Scaled size, in pixels */
        struct {
            int width;
            int height;
        } px_scaled;
    } dim;

    struct {
        /* PPI, based on physical size */
        struct {
            int x;
            int y;
        } real;

        /* PPI, logical, based on scaled size */
        struct {
            int x;
            int y;
        } scaled;
    } ppi;

    /* From wl_output */
    char *make;
    char *model;

    /* From xdg_output */
    char *name;
    char *description;

    float inch;  /* e.g. 24" */
    float refresh;

    int scale;
    enum fcft_subpixel subpixel;
};

struct repeat {
    int fd;
    int32_t delay;
    int32_t rate;

    bool dont_re_repeat;
    uint32_t key;
};

struct seat {
    struct wayland *wayl;
    struct wl_seat *wl_seat;
    uint32_t wl_name;
    char *name;

    struct wl_keyboard *wl_keyboard;
    struct {
        uint32_t serial;
        struct xkb_context *xkb;
        struct xkb_keymap *xkb_keymap;
        struct xkb_state *xkb_state;
        struct xkb_compose_table *xkb_compose_table;
        struct xkb_compose_state *xkb_compose_state;
        struct repeat repeat;
    } kbd;

    struct wl_pointer *wl_pointer;
    struct {
        uint32_t serial;

        int x;
        int y;

        struct wl_surface *surface;
        struct wl_cursor_theme *theme;
        struct wl_cursor *cursor;
        int scale;
    } pointer;
};

struct wayland {
    struct fdm *fdm;
    struct render *render;
    struct prompt *prompt;
    struct matches *matches;

    const struct render_options *render_options;
    char *font_name;
    const icon_theme_list_t *themes;
    struct application_list *apps;

    int width;
    int height;
    int scale;
    unsigned dpi;

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
    struct zxdg_output_manager_v1 *xdg_output_manager;

    char *output_name;
    tll(struct monitor) monitors;
    const struct monitor *monitor;

    tll(struct seat) seats;
};

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
seat_destroy(struct seat *seat)
{
    if (seat == NULL)
        return;

    if (seat->kbd.xkb_compose_state != NULL)
        xkb_compose_state_unref(seat->kbd.xkb_compose_state);
    if (seat->kbd.xkb_compose_table != NULL)
        xkb_compose_table_unref(seat->kbd.xkb_compose_table);
    if (seat->kbd.xkb_state != NULL)
        xkb_state_unref(seat->kbd.xkb_state);
    if (seat->kbd.xkb_keymap != NULL)
        xkb_keymap_unref(seat->kbd.xkb_keymap);
    if (seat->kbd.xkb != NULL)
        xkb_context_unref(seat->kbd.xkb);
    if (seat->kbd.repeat.fd > 0)
        fdm_del(seat->wayl->fdm, seat->kbd.repeat.fd);
    if (seat->wl_keyboard != NULL)
        wl_keyboard_destroy(seat->wl_keyboard);

    if (seat->pointer.theme != NULL)
        wl_cursor_theme_destroy(seat->pointer.theme);
    if (seat->pointer.surface != NULL)
        wl_surface_destroy(seat->pointer.surface);
    if (seat->wl_pointer != NULL)
        wl_pointer_destroy(seat->wl_pointer);

    if (seat->wl_seat != NULL)
        wl_seat_destroy(seat->wl_seat);

    free(seat->name);
}

static void
update_cursor_surface(struct seat *seat)
{
    if (seat->pointer.surface == NULL || seat->pointer.theme == NULL)
        return;

    if (seat->pointer.cursor == NULL) {
        seat->pointer.cursor = wl_cursor_theme_get_cursor(
            seat->pointer.theme, "left_ptr");

        if (seat->pointer.cursor == NULL) {
            LOG_ERR("%s: failed to load cursor 'left_ptr'", seat->name);
            return;
        }
    }

    struct wl_cursor_image *image = seat->pointer.cursor->images[0];

    const int scale = seat->pointer.scale;
    wl_surface_set_buffer_scale(seat->pointer.surface, scale);

    wl_surface_attach(
        seat->pointer.surface, wl_cursor_image_get_buffer(image), 0, 0);

    wl_pointer_set_cursor(
        seat->wl_pointer, seat->pointer.serial, seat->pointer.surface,
        image->hotspot_x / scale, image->hotspot_y / scale);

    wl_surface_damage_buffer(seat->pointer.surface, 0, 0, INT32_MAX, INT32_MAX);

    wl_surface_commit(seat->pointer.surface);
    wl_display_flush(seat->wayl->display);
}

static bool
reload_cursor_theme(struct seat *seat, int new_scale)
{
    assert(seat->pointer.surface != NULL);

    if (seat->pointer.theme != NULL && seat->pointer.scale == new_scale)
        return true;

    if (seat->pointer.theme != NULL) {
        wl_cursor_theme_destroy(seat->pointer.theme);
        seat->pointer.theme = NULL;
        seat->pointer.cursor = NULL;
    }

    const char *xcursor_theme = getenv("XCURSOR_THEME");
    int xcursor_size = 24;

    {
        const char *env_cursor_size = getenv("XCURSOR_SIZE");
        if (env_cursor_size != NULL) {
            unsigned size;
            if (sscanf(env_cursor_size, "%u", &size) == 1)
                xcursor_size = size;
        }
    }

    LOG_INFO("cursor theme: %s, size: %u, scale: %d",
             xcursor_theme, xcursor_size, new_scale);

    seat->pointer.theme = wl_cursor_theme_load(
        xcursor_theme, xcursor_size * new_scale, seat->wayl->shm);

    if (seat->pointer.theme == NULL) {
        LOG_ERR("failed to load cursor theme");
        return false;
    }

    seat->pointer.scale = new_scale;
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
    struct seat *seat = data;

    char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (seat->kbd.xkb_compose_state != NULL) {
        xkb_compose_state_unref(seat->kbd.xkb_compose_state);
        seat->kbd.xkb_compose_state = NULL;
    }
    if (seat->kbd.xkb_compose_table != NULL) {
        xkb_compose_table_unref(seat->kbd.xkb_compose_table);
        seat->kbd.xkb_compose_table = NULL;
    }
    if (seat->kbd.xkb_keymap != NULL) {
        xkb_keymap_unref(seat->kbd.xkb_keymap);
        seat->kbd.xkb_keymap = NULL;
    }
    if (seat->kbd.xkb_state != NULL) {
        xkb_state_unref(seat->kbd.xkb_state);
        seat->kbd.xkb_state = NULL;
    }
    if (seat->kbd.xkb != NULL) {
        xkb_context_unref(seat->kbd.xkb);
        seat->kbd.xkb = NULL;
    }

    seat->kbd.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    seat->kbd.xkb_keymap = xkb_keymap_new_from_string(
        seat->kbd.xkb, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    /* TODO: initialize in enter? */
    seat->kbd.xkb_state = xkb_state_new(seat->kbd.xkb_keymap);

    /* Compose (dead keys) */
    seat->kbd.xkb_compose_table = xkb_compose_table_new_from_locale(
        seat->kbd.xkb, setlocale(LC_CTYPE, NULL), XKB_COMPOSE_COMPILE_NO_FLAGS);
    seat->kbd.xkb_compose_state = xkb_compose_state_new(
        seat->kbd.xkb_compose_table, XKB_COMPOSE_STATE_NO_FLAGS);

    munmap(map_str, size);
    close(fd);
}

static void
keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
               struct wl_surface *surface, struct wl_array *keys)
{
    struct seat *seat = data;
    seat->kbd.serial = serial;
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
    struct seat *seat = data;
    repeat_stop(&seat->kbd.repeat, -1);
    seat->kbd.serial = 0;
    seat->wayl->status = EXIT;
}

static void
keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
             uint32_t time, uint32_t key, uint32_t state)
{
    static bool mod_masks_initialized = false;
    static xkb_mod_mask_t ctrl = -1;
    static xkb_mod_mask_t alt = -1;
    //static xkb_mod_mask_t shift = -1;

    struct seat *seat = data;
    struct wayland *wayl = seat->wayl;

    if (!mod_masks_initialized) {
        mod_masks_initialized = true;
        ctrl = 1 << xkb_keymap_mod_get_index(seat->kbd.xkb_keymap, "Control");
        alt = 1 << xkb_keymap_mod_get_index(seat->kbd.xkb_keymap, "Mod1");
        //shift = 1 << xkb_keymap_mod_get_index(seat->kbd.xkb_keymap, "Shift");
    }

    if (state == XKB_KEY_UP) {
        repeat_stop(&seat->kbd.repeat, key);
        return;
    }

    key += 8;
    bool should_repeat = xkb_keymap_key_repeats(seat->kbd.xkb_keymap, key);
    xkb_keysym_t sym = xkb_state_key_get_one_sym(seat->kbd.xkb_state, key);

    xkb_compose_state_feed(seat->kbd.xkb_compose_state, sym);
    enum xkb_compose_status compose_status = xkb_compose_state_get_status(
        seat->kbd.xkb_compose_state);

    if (compose_status == XKB_COMPOSE_COMPOSING) {
        /* TODO: goto maybe_repeat? */
        return;
    }

    xkb_mod_mask_t mods = xkb_state_serialize_mods(
        seat->kbd.xkb_state, XKB_STATE_MODS_EFFECTIVE);
    xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods(seat->kbd.xkb_state, key);
    xkb_mod_mask_t significant = ctrl | alt /*| shift*/;
    xkb_mod_mask_t effective_mods = mods & ~consumed & significant;

#if 0
    for (size_t i = 0; i < 32; i++) {
        if (mods & (1 << i)) {
            LOG_DBG("%s", xkb_keymap_mod_get_name(seat->kbd.xkb_keymap, i));
        }
    }
#endif

    LOG_DBG("sym=%u, mod=0x%08x, consumed=0x%08x, significant=0x%08x, "
            "effective=0x%08x",
            sym, mods, consumed, significant, effective_mods);

    if (sym == XKB_KEY_Home || (sym == XKB_KEY_a && effective_mods == ctrl)) {
        if (prompt_cursor_home(wayl->prompt))
            wayl_refresh(wayl);
    }

    else if (sym == XKB_KEY_End || (sym == XKB_KEY_e && effective_mods == ctrl)) {
        if (prompt_cursor_end(wayl->prompt))
            wayl_refresh(wayl);
    }

    else if ((sym == XKB_KEY_b && effective_mods == alt) ||
             (sym == XKB_KEY_Left && effective_mods == ctrl)) {
        if (prompt_cursor_prev_word(wayl->prompt))
            wayl_refresh(wayl);
    }

    else if ((sym == XKB_KEY_f && effective_mods == alt) ||
             (sym == XKB_KEY_Right && effective_mods == ctrl)) {

        if (prompt_cursor_next_word(wayl->prompt))
            wayl_refresh(wayl);
    }

    else if ((sym == XKB_KEY_Escape && effective_mods == 0) ||
             (sym == XKB_KEY_g && effective_mods == ctrl)) {
        wayl->status = EXIT;
    }

    else if ((sym == XKB_KEY_p && effective_mods == ctrl) ||
             (sym == XKB_KEY_Up && effective_mods == 0)) {
        if (matches_selected_prev(wayl->matches, false))
            wayl_refresh(wayl);
    }

    else if ((sym == XKB_KEY_n && effective_mods == ctrl) ||
             (sym == XKB_KEY_Down && effective_mods == 0)) {
        if (matches_selected_next(wayl->matches, false))
            wayl_refresh(wayl);
    }

    else if (sym == XKB_KEY_Tab && effective_mods == 0) {
        if (matches_selected_next(wayl->matches, true))
            wayl_refresh(wayl);
    }

    else if (sym == XKB_KEY_ISO_Left_Tab && effective_mods == 0) {
        if (matches_selected_prev(wayl->matches, true))
            wayl_refresh(wayl);
    }

    else if ((sym == XKB_KEY_b && effective_mods == ctrl) ||
             (sym == XKB_KEY_Left && effective_mods == 0)) {
        if (prompt_cursor_prev_char(wayl->prompt))
            wayl_refresh(wayl);
    }

    else if ((sym == XKB_KEY_f && effective_mods == ctrl) ||
             (sym == XKB_KEY_Right && effective_mods == 0)) {
        if (prompt_cursor_next_char(wayl->prompt))
            wayl_refresh(wayl);
    }

    else if ((sym == XKB_KEY_d && effective_mods == ctrl) ||
             (sym == XKB_KEY_Delete && effective_mods == 0)) {
        if (prompt_erase_next_char(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            wayl_refresh(wayl);
        }
    }

    else if (sym == XKB_KEY_BackSpace && effective_mods == 0) {
        if (prompt_erase_prev_char(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            wayl_refresh(wayl);
        }
    }

    else if (sym == XKB_KEY_BackSpace && (effective_mods == ctrl ||
                                          effective_mods == alt)) {
        if (prompt_erase_prev_word(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            wayl_refresh(wayl);
        }
    }

    else if ((sym == XKB_KEY_d && effective_mods == alt) ||
             (sym == XKB_KEY_Delete && effective_mods == ctrl)) {
        if (prompt_erase_next_word(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            wayl_refresh(wayl);
        }
    }

    else if (sym == XKB_KEY_k && effective_mods == ctrl) {
        if (prompt_erase_after_cursor(wayl->prompt)) {
            matches_update(wayl->matches, wayl->prompt);
            wayl_refresh(wayl);
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
        /*
         * Compose, and maybe emit "normal" character
         */

        char buf[64] = {0};
        int count = 0;

        if (compose_status == XKB_COMPOSE_COMPOSED) {
            count = xkb_compose_state_get_utf8(
                seat->kbd.xkb_compose_state, buf, sizeof(buf));
            xkb_compose_state_reset(seat->kbd.xkb_compose_state);
        } else if (compose_status == XKB_COMPOSE_CANCELLED) {
            goto maybe_repeat;
        } else {
            count = xkb_state_key_get_utf8(
                seat->kbd.xkb_state, key, buf, sizeof(buf));
        }

        if (count == 0)
            return;

        if (!prompt_insert_chars(wayl->prompt, buf, count))
            return;

        matches_update(wayl->matches, wayl->prompt);
        wayl_refresh(wayl);
    }

maybe_repeat:

    if (should_repeat)
        repeat_start(&seat->kbd.repeat, key - 8);

}

static void
keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                   uint32_t mods_depressed, uint32_t mods_latched,
                   uint32_t mods_locked, uint32_t group)
{
    struct seat *seat = data;

    LOG_DBG("modifiers: depressed=0x%x, latched=0x%x, locked=0x%x, group=%u",
            mods_depressed, mods_latched, mods_locked, group);

    xkb_state_update_mask(
        seat->kbd.xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                     int32_t rate, int32_t delay)
{
    struct seat *seat = data;
    LOG_DBG("keyboard repeat: rate=%d, delay=%d", rate, delay);
    seat->kbd.repeat.delay = delay;
    seat->kbd.repeat.rate = rate;
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
wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface,
                 wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct seat *seat = data;
    seat->pointer.serial = serial;
    reload_cursor_theme(seat, seat->wayl->scale);
    update_cursor_surface(seat);
}

static void
wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                 uint32_t serial, struct wl_surface *surface)
{
    struct seat *seat = data;
    seat->pointer.serial = serial;
}

static void
wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                  uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
}

static void
wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                  uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
}

static void
wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static void
wl_pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
}

static void
wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                       uint32_t axis_source)
{
}

static void
wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                     uint32_t time, uint32_t axis)
{
}

static void
wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                         uint32_t axis, int32_t discrete)
{
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
    .frame = wl_pointer_frame,
    .axis_source = wl_pointer_axis_source,
    .axis_stop = wl_pointer_axis_stop,
    .axis_discrete = wl_pointer_axis_discrete,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                         enum wl_seat_capability caps)
{
    struct seat *seat = data;
    assert(seat->wl_seat == wl_seat);

    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (seat->wl_keyboard == NULL) {
            seat->wl_keyboard = wl_seat_get_keyboard(wl_seat);
            wl_keyboard_add_listener(seat->wl_keyboard, &keyboard_listener, seat);
        }
    } else {
        if (seat->wl_keyboard != NULL) {
            wl_keyboard_release(seat->wl_keyboard);
            seat->wl_keyboard = NULL;
        }
    }

    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        if (seat->wl_pointer == NULL) {
            assert(seat->pointer.surface == NULL);
            seat->pointer.surface = wl_compositor_create_surface(seat->wayl->compositor);

            if (seat->pointer.surface == NULL) {
                LOG_ERR("%s: failed to create pointer surface", seat->name);
                return;
            }

            seat->wl_pointer = wl_seat_get_pointer(wl_seat);
            wl_pointer_add_listener(seat->wl_pointer, &pointer_listener, seat);
        }
    } else {
        if (seat->wl_pointer != NULL) {
            wl_pointer_release(seat->wl_pointer);
            wl_surface_destroy(seat->pointer.surface);

            if (seat->pointer.theme != NULL)
                wl_cursor_theme_destroy(seat->pointer.theme);

            seat->wl_pointer = NULL;
            seat->pointer.surface = NULL;
            seat->pointer.theme = NULL;
            seat->pointer.cursor = NULL;
        }
    }
}

static void
seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name)
{
    struct seat *seat = data;
    free(seat->name);
    seat->name = strdup(name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static int
guess_scale(const struct wayland *wayl)
{
    if (tll_length(wayl->monitors) == 0)
        return 1;

    bool all_have_same_scale = true;
    int last_scale = -1;

    tll_foreach(wayl->monitors, it) {
        if (last_scale == -1)
            last_scale = it->item.scale;
        else if (last_scale != it->item.scale) {
            all_have_same_scale = false;
            break;
        }
    }

    if (all_have_same_scale) {
        assert(last_scale >= 1);
        return last_scale;
    }

    return 1;
}

static bool
reload_font(struct wayland *wayl, unsigned new_dpi, unsigned new_scale)
{
    LOG_DBG("font reload: scale: %u -> %u, dpi: %u -> %u",
            wayl->scale, new_scale, wayl->dpi, new_dpi);

    struct fcft_font *font = NULL;

    if (wayl->dpi != new_dpi) {
        char attrs[256];
        snprintf(attrs, sizeof(attrs), "dpi=%u", new_dpi);

        font = fcft_from_name(1, (const char *[]){wayl->font_name}, attrs);
        if (font == NULL)
            return false;

        icon_reload_application_icons(*wayl->themes, font->height, wayl->apps);
    }

    return render_set_font(
        wayl->render, font, new_scale, &wayl->width, &wayl->height);
}

static void
update_size(struct wayland *wayl)
{
    const struct monitor *mon = wayl->monitor;
    const int scale = mon != NULL ? mon->scale : guess_scale(wayl);
    const int dpi = mon != NULL ? mon->ppi.scaled.y * mon->scale : wayl_ppi(wayl);

    if (scale == wayl->scale && dpi == wayl->dpi)
        return;

    reload_font(wayl, dpi, scale);

    wayl->scale = scale;
    wayl->dpi = dpi;

    wayl->width /= scale; wayl->width *= scale;
    wayl->height /= scale; wayl->height *= scale;

    zwlr_layer_surface_v1_set_size(
        wayl->layer_surface, wayl->width / scale, wayl->height / scale);

    /* Trigger a 'configure' event, after which we'll have the width */
    wl_surface_commit(wayl->surface);
    wl_display_roundtrip(wayl->display);
}

static void
output_update_ppi(struct monitor *mon)
{
    if (mon->dim.mm.width == 0 || mon->dim.mm.height == 0)
        return;

    int x_inches = mon->dim.mm.width * 0.03937008;
    int y_inches = mon->dim.mm.height * 0.03937008;
    mon->ppi.real.x = mon->dim.px_real.width / x_inches;
    mon->ppi.real.y = mon->dim.px_real.height / y_inches;

    mon->ppi.scaled.x = mon->dim.px_scaled.width / x_inches;
    mon->ppi.scaled.y = mon->dim.px_scaled.height / y_inches;
}

static void
output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                int32_t physical_width, int32_t physical_height,
                int32_t subpixel, const char *make, const char *model,
                int32_t transform)
{
    struct monitor *mon = data;
    mon->dim.mm.width = physical_width;
    mon->dim.mm.height = physical_height;
    mon->inch = sqrt(pow(mon->dim.mm.width, 2) + pow(mon->dim.mm.height, 2)) * 0.03937008;
    mon->make = make != NULL ? strdup(make) : NULL;
    mon->model = model != NULL ? strdup(model) : NULL;
    mon->subpixel = subpixel;
    output_update_ppi(mon);
}

static void
output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
            int32_t width, int32_t height, int32_t refresh)
{
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0)
        return;

    struct monitor *mon = data;
    mon->refresh = (float)refresh / 1000;
    mon->dim.px_real.width = width;
    mon->dim.px_real.height = height;
    output_update_ppi(mon);
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

    if (mon->wayl->monitor == mon) {
        int old_scale = mon->wayl->scale;
        int old_dpi = mon->wayl->dpi;

        update_size(mon->wayl);

        if (mon->wayl->scale != old_scale || mon->wayl->dpi != old_dpi)
            wayl_refresh(mon->wayl);
    }
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
    mon->dim.px_scaled.width = width;
    mon->dim.px_scaled.height = height;
    output_update_ppi(mon);
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
    struct wayland *wayl = mon->wayl;

    mon->name = name != NULL ? strdup(name) : NULL;

    if (wayl->output_name != NULL &&
        mon->name != NULL &&
        strcmp(wayl->output_name, mon->name) == 0)
    {
        wayl->monitor = mon;
    }
}

static void
xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output,
                              const char *description)
{
    struct monitor *mon = data;
    mon->description = description != NULL ? strdup(description) : NULL;
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

static bool
fdm_repeat(struct fdm *fdm, int fd, int events, void *data)
{
    struct seat *seat = data;
    struct repeat *repeat = &seat->kbd.repeat;

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
        keyboard_key(seat, NULL, 0, 0, repeat->key, XKB_KEY_DOWN);
    repeat->dont_re_repeat = false;

    if (events & EPOLLHUP) {
        LOG_ERR("keyboard repeater timer FD closed unexpectedly");
        return false;
    }

    return true;
}

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    LOG_DBG("global: 0x%08x, interface=%s, version=%u", name, interface, version);
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
    }

    else if (strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t required = 3;
        if (!verify_iface_version(interface, version, required))
            return;

        struct wl_output *output = wl_registry_bind(
            wayl->registry, name, &wl_output_interface, required);

        tll_push_back(wayl->monitors, ((struct monitor){
            .output = output,
            .wayl = wayl,
            .wl_name = name,
        }));

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

        struct wl_seat *wl_seat = wl_registry_bind(
            wayl->registry, name, &wl_seat_interface, required);

        int repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
        if (repeat_fd == -1) {
            LOG_ERRNO("failed to create keyboard repeat timer FD");
            wl_seat_destroy(wl_seat);
            return;
        }

        tll_push_back(wayl->seats, ((struct seat){
                    .wayl = wayl,
                        .wl_seat = wl_seat,
                        .wl_name = name,
                        .kbd = {
                        .repeat = {
                            .fd = repeat_fd,
                        },
                    }}));

        struct seat *seat = &tll_back(wayl->seats);

        if (!fdm_add(wayl->fdm, repeat_fd, EPOLLIN, &fdm_repeat, seat)) {
            LOG_ERR("failed to register keyboard repeat timer FD with FDM");
            close(seat->kbd.repeat.fd);
            wl_seat_destroy(seat->wl_seat);
            tll_pop_back(wayl->seats);
            return;
        }

        wl_seat_add_listener(wl_seat, &seat_listener, seat);
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
monitor_destroy(struct monitor *mon)
{
    if (mon->xdg != NULL)
        zxdg_output_v1_destroy(mon->xdg);
    if (mon->output != NULL)
        wl_output_destroy(mon->output);
    free(mon->name);
    free(mon->make);
    free(mon->model);
    free(mon->description);
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    LOG_DBG("global removed: 0x%08x", name);

    struct wayland *wayl = data;
    tll_foreach(wayl->monitors, it) {
        struct monitor *mon = &it->item;

        if (mon->wl_name != name)
            continue;

        LOG_INFO("monitor disabled: %s", mon->name);

        if (wayl->monitor == mon)
            wayl->monitor = NULL;

        monitor_destroy(mon);
        tll_remove(wayl->monitors, it);
        return;
    }

    tll_foreach(wayl->seats, it) {
        struct seat *seat = &it->item;

        if (seat->wl_name != name)
            continue;

        LOG_INFO("seat removed: %s", seat->name);

        seat_destroy(seat);
        tll_remove(wayl->seats, it);
        return;
    }

    LOG_WARN("unknown global removed: 0x%08x", name);
}

static const struct wl_registry_listener registry_listener = {
    .global = &handle_global,
    .global_remove = &handle_global_remove,
};

static void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                        uint32_t serial, uint32_t w, uint32_t h)
{
    struct wayland *wayl = data;

    if (w > 0 && h > 0) {
        if (w * wayl->scale != wayl->width || h * wayl->scale != wayl->height) {
            wayl->width = w * wayl->scale;
            wayl->height = h * wayl->scale;
            wayl_refresh(wayl);
        }
    }

    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void
layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
    struct wayland *wayl = data;;
    wayl->status = EXIT;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = &layer_surface_configure,
    .closed = &layer_surface_closed,
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

    assert(wayl->scale >= 1);

    wl_surface_set_buffer_scale(wayl->surface, wayl->scale);
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

void
wayl_refresh(struct wayland *wayl)
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
    int event_count = 0;

    if (events & EPOLLIN)
        wl_display_read_events(wayl->display);

    while (wl_display_prepare_read(wayl->display) != 0)
        wl_display_dispatch_pending(wayl->display);

    if (events & EPOLLHUP) {
        LOG_WARN("disconnected from Wayland");
        wl_display_cancel_read(wayl->display);
        return false;
    }

    wl_display_flush(wayl->display);
    return event_count != -1 && wayl->status == KEEP_RUNNING;
}

static void
surface_enter(void *data, struct wl_surface *wl_surface,
              struct wl_output *wl_output)
{
    struct wayland *wayl = data;

    tll_foreach(wayl->monitors, it) {
        if (it->item.output != wl_output)
            continue;

        int old_scale = wayl->scale;
        int old_dpi = wayl->dpi;

        wayl->monitor = &it->item;
        update_size(wayl);

        if (wayl->scale != old_scale || wayl->dpi != old_dpi)
            wayl_refresh(wayl);
        break;
    }
}

static void
surface_leave(void *data, struct wl_surface *wl_surface,
              struct wl_output *wl_output)
{
    struct wayland *wayl = data;
    wayl->monitor = NULL;
}

static const struct wl_surface_listener surface_listener = {
    .enter = &surface_enter,
    .leave = &surface_leave,
};

struct wayland *
wayl_init(struct fdm *fdm,
          struct render *render, struct prompt *prompt, struct matches *matches,
          const struct render_options *render_options, bool dmenu_mode,
          const char *output_name, const char *font_name,

          const icon_theme_list_t *themes, struct application_list *apps)
{
    struct wayland *wayl = malloc(sizeof(*wayl));
    *wayl = (struct wayland){
        .fdm = fdm,
        .render = render,
        .prompt = prompt,
        .matches = matches,
        .status = KEEP_RUNNING,
        .exit_code = EXIT_FAILURE,
        .dmenu_mode = dmenu_mode,
        .output_name = output_name != NULL ? strdup(output_name) : NULL,
        .font_name = strdup(font_name),
        .render_options = render_options,
        .themes = themes,
        .apps = apps,
    };

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

    wl_display_roundtrip(wayl->display);

    if (tll_length(wayl->monitors) == 0) {
        LOG_ERR("no monitors");
        goto out;
    }

    tll_foreach(wayl->monitors, it) {
        const struct monitor *mon = &it->item;
        LOG_INFO(
            "%s: %dx%d+%dx%d@%dHz %s %.2f\" scale=%d PPI=%dx%d (physical) PPI=%dx%d (logical)",
            mon->name, mon->dim.px_real.width, mon->dim.px_real.height,
            mon->x, mon->y, (int)round(mon->refresh),
            mon->model != NULL ? mon->model : mon->description,
            mon->inch, mon->scale,
            mon->ppi.real.x, mon->ppi.real.y,
            mon->ppi.scaled.x, mon->ppi.scaled.y);
    }

    LOG_DBG("using output: %s",
            wayl->monitor != NULL ? wayl->monitor->name : NULL);

    wayl->surface = wl_compositor_create_surface(wayl->compositor);
    if (wayl->surface == NULL) {
        LOG_ERR("failed to create panel surface");
        goto out;
    }

    wl_surface_add_listener(wayl->surface, &surface_listener, wayl);

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
        wayl->layer_shell, wayl->surface,
        wayl->monitor != NULL ? wayl->monitor->output : NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "launcher");

    if (wayl->layer_surface == NULL) {
        LOG_ERR("failed to create layer shell surface");
        goto out;
    }

    zwlr_layer_surface_v1_set_keyboard_interactivity(wayl->layer_surface, 1);

    zwlr_layer_surface_v1_add_listener(
        wayl->layer_surface, &layer_surface_listener, wayl);

    update_size(wayl);

    if (wl_display_prepare_read(wayl->display) != 0) {
        LOG_ERRNO("failed to prepare for reading wayland events");
        goto out;
    }

    if (!fdm_add(wayl->fdm, wl_display_get_fd(wayl->display), EPOLLIN, &fdm_handler, wayl)) {
        LOG_ERR("failed to register Wayland socket with FDM");
        goto out;
    }

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

    tll_foreach(wayl->seats, it)
        seat_destroy(&it->item);
    tll_free(wayl->seats);

    tll_foreach(wayl->monitors, it)
        monitor_destroy(&it->item);
    tll_free(wayl->monitors);

    if (wayl->xdg_output_manager != NULL)
        zxdg_output_manager_v1_destroy(wayl->xdg_output_manager);

    if (wayl->layer_surface != NULL)
        zwlr_layer_surface_v1_destroy(wayl->layer_surface);
    if (wayl->layer_shell != NULL)
        zwlr_layer_shell_v1_destroy(wayl->layer_shell);
    if (wayl->surface != NULL)
        wl_surface_destroy(wayl->surface);
    if (wayl->compositor != NULL)
        wl_compositor_destroy(wayl->compositor);
    if (wayl->shm != NULL)
        wl_shm_destroy(wayl->shm);
    if (wayl->registry != NULL)
        wl_registry_destroy(wayl->registry);
    if (wayl->display != NULL)
        wl_display_disconnect(wayl->display);

    free(wayl->output_name);
    free(wayl->font_name);
    free(wayl);
}

void
wayl_flush(struct wayland *wayl)
{
    wl_display_flush(wayl->display);
}

unsigned
wayl_ppi(const struct wayland *wayl)
{
    /* Use user configured output, if available, otherwise use the
     * "first" output */
    const struct monitor *mon = wayl->monitor != NULL
        ? wayl->monitor
        : (tll_length(wayl->monitors) > 0
           ? &tll_front(wayl->monitors)
           : NULL);

    if (mon != NULL)
        return mon->ppi.scaled.y * mon->scale;

    /* No outputs available, return "something" */
    return 96u;
}

enum fcft_subpixel
wayl_subpixel(const struct wayland *wayl)
{
    return wayl->monitor != NULL
        ? wayl->monitor->subpixel : FCFT_SUBPIXEL_DEFAULT;
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
