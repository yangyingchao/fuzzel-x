#include "wayland.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
#include <fontconfig/fontconfig.h>

#include <xdg-output-unstable-v1.h>
#include <wlr-layer-shell-unstable-v1.h>
#include <xdg-activation-v1.h>

#include <tllist.h>

#define LOG_MODULE "wayland"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "dmenu.h"
#include "icon.h"
#include "key-binding.h"
#include "prompt.h"
#include "render.h"
#include "shm.h"

struct font_spec {
    char *pattern;
    double pt_size;
    int px_size;
};

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

    float dpi;

    enum wl_output_transform transform;

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
    const struct config *conf;
    struct fdm *fdm;
    struct kb_manager *kb_manager;
    struct render *render;
    struct prompt *prompt;
    struct matches *matches;

    struct font_spec *fonts;
    size_t font_count;
    struct {
        font_reloaded_t cb;
        void *data;
    } font_reloaded;

    bool render_first_frame_transparent;
    int width;
    int height;
    int scale;
    float dpi;
    enum fcft_subpixel subpixel;
    bool font_is_sized_by_dpi;

    enum { KEEP_RUNNING, EXIT_UPDATE_CACHE, EXIT} status;
    int exit_code;

    struct wl_callback *frame_cb;
    struct buffer *pending;

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_shm *shm;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    struct xdg_activation_v1 *xdg_activation_v1;

    tll(struct monitor) monitors;
    const struct monitor *monitor;

    tll(struct seat) seats;
};

bool
repeat_start(struct repeat *repeat, uint32_t key)
{
    if (repeat->dont_re_repeat)
        return true;

    if (repeat->rate == 0)
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

    kb_remove_seat(seat->wayl->kb_manager, seat);

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
        wl_keyboard_release(seat->wl_keyboard);

    if (seat->pointer.theme != NULL)
        wl_cursor_theme_destroy(seat->pointer.theme);
    if (seat->pointer.surface != NULL)
        wl_surface_destroy(seat->pointer.surface);
    if (seat->wl_pointer != NULL)
        wl_pointer_release(seat->wl_pointer);

    if (seat->wl_seat != NULL)
        wl_seat_release(seat->wl_seat);

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

    kb_unload_keymap(seat->wayl->kb_manager, seat);

    /* Verify keymap is in a format we understand */
    switch ((enum wl_keyboard_keymap_format)format) {
    case WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP:
        return;

    case WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1:
        break;

    default:
        LOG_WARN("unrecognized keymap format: %u", format);
        return;
    }

    char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED) {
        LOG_ERRNO("failed to mmap keyboard keymap");
        close(fd);
        return;
    }

    while (map_str[size - 1] == '\0')
        size--;

    seat->kbd.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (seat->kbd.xkb != NULL) {
        seat->kbd.xkb_keymap = xkb_keymap_new_from_buffer(
            seat->kbd.xkb, map_str, size, XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);

        /* Compose (dead keys) */
        seat->kbd.xkb_compose_table = xkb_compose_table_new_from_locale(
            seat->kbd.xkb, setlocale(LC_CTYPE, NULL), XKB_COMPOSE_COMPILE_NO_FLAGS);

        if (seat->kbd.xkb_compose_table == NULL) {
            LOG_WARN("failed to instantiate compose table; dead keys will not work");
        } else {
            seat->kbd.xkb_compose_state = xkb_compose_state_new(
                seat->kbd.xkb_compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
        }
    }

    if (seat->kbd.xkb_keymap != NULL) {
        /* TODO: initialize in enter? */
        seat->kbd.xkb_state = xkb_state_new(seat->kbd.xkb_keymap);
    }

    munmap(map_str, size);
    close(fd);

    if (seat->kbd.xkb_state != NULL && seat->kbd.xkb_keymap != NULL) {
        kb_load_keymap(seat->wayl->kb_manager, seat,
                       seat->kbd.xkb_state, seat->kbd.xkb_keymap);
    }
}

static void
keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
               struct wl_surface *surface, struct wl_array *keys)
{
    struct seat *seat = data;
    seat->kbd.serial = serial;

    LOG_DBG("keyboard enter");
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

    if (seat->wayl->conf->exit_on_kb_focus_loss)
        seat->wayl->status = EXIT;
}

static void
token_done(void *data, struct xdg_activation_token_v1 *token,
           const char *name)
{
    char **xdg_activation_token = data;
    *xdg_activation_token = strdup(name);
}

static const struct xdg_activation_token_v1_listener token_listener = {
    .done = token_done
};


/* Synchronously grab a token from the compositor. The compositor may refuse to
 * grant a token by not sending the done event. In this case, the
 * xdg_activation_token return parameter will not be modified.
 */
static void
get_xdg_activation_token(struct seat *seat, const char *app_id,
                         char **xdg_activation_token)
{
    struct wayland *wayl = seat->wayl;
    struct xdg_activation_token_v1 *token =
        xdg_activation_v1_get_activation_token(wayl->xdg_activation_v1);

    xdg_activation_token_v1_set_serial(token, seat->kbd.serial, seat->wl_seat);
    xdg_activation_token_v1_set_surface(token, wayl->surface);

    if (app_id != NULL)
        xdg_activation_token_v1_set_app_id(token, app_id);

    xdg_activation_token_v1_add_listener(
        token, &token_listener, xdg_activation_token);

    xdg_activation_token_v1_commit(token);
    wl_display_roundtrip(wayl->display);
    xdg_activation_token_v1_destroy(token);
}

static void
execute_selected(struct seat *seat, int custom_success_exit_code)
{
    struct wayland *wayl = seat->wayl;
    wayl->status = EXIT;

    const struct match *match = matches_get_match(wayl->matches);
    struct application *app = match != NULL ? match->application : NULL;
    ssize_t index = match != NULL ? match->index : -1;


    if (wayl->conf->dmenu.enabled) {
        dmenu_execute(app, index, wayl->prompt, wayl->conf->dmenu.mode);
        wayl->exit_code = custom_success_exit_code >= 0
            ? custom_success_exit_code
            : EXIT_SUCCESS;
    } else {
        char *xdg_activation_token = NULL;
        if (wayl->xdg_activation_v1 != NULL)
            get_xdg_activation_token(seat, app->app_id, &xdg_activation_token);

        bool success = application_execute(
            app, wayl->prompt, wayl->conf->launch_prefix, xdg_activation_token);
        free(xdg_activation_token);

        wayl->exit_code = success
            ? (custom_success_exit_code >= 0
               ? custom_success_exit_code
               : EXIT_SUCCESS)
            : EXIT_FAILURE;

        if (success && match != NULL) {
            wayl->status = EXIT_UPDATE_CACHE;
            app->count++;
        }
    }
}


static bool
execute_binding(struct seat *seat, const struct key_binding *binding, bool *refresh)
{
    const enum bind_action action = binding->action;
    struct wayland *wayl = seat->wayl;

    *refresh = false;

    switch (action) {
    case BIND_ACTION_NONE:
        return true;

    case BIND_ACTION_CANCEL:
        wayl->status = EXIT;
        return true;

    case BIND_ACTION_CURSOR_HOME:
        *refresh = prompt_cursor_home(wayl->prompt);
        return true;

    case BIND_ACTION_CURSOR_END:
        *refresh = prompt_cursor_end(wayl->prompt);
        return true;

    case BIND_ACTION_CURSOR_LEFT:
        *refresh = prompt_cursor_prev_char(wayl->prompt);
        return true;

    case BIND_ACTION_CURSOR_LEFT_WORD:
        *refresh = prompt_cursor_prev_word(wayl->prompt);
        return true;

    case BIND_ACTION_CURSOR_RIGHT:
        *refresh = prompt_cursor_next_char(wayl->prompt);
        return true;

    case BIND_ACTION_CURSOR_RIGHT_WORD:
        *refresh = prompt_cursor_next_word(wayl->prompt);
        return true;

    case BIND_ACTION_DELETE_PREV:
        *refresh = prompt_erase_prev_char(wayl->prompt);
        if (*refresh)
            matches_update(wayl->matches, wayl->prompt);
        return true;

    case BIND_ACTION_DELETE_PREV_WORD:
        *refresh = prompt_erase_prev_word(wayl->prompt);
        if (*refresh)
            matches_update(wayl->matches, wayl->prompt);
        return true;

    case BIND_ACTION_DELETE_NEXT:
        *refresh = prompt_erase_next_char(wayl->prompt);
        if (*refresh)
            matches_update(wayl->matches, wayl->prompt);
        return true;

    case BIND_ACTION_DELETE_NEXT_WORD:
        *refresh = prompt_erase_next_word(wayl->prompt);
        if (*refresh)
            matches_update(wayl->matches, wayl->prompt);
        return true;

    case BIND_ACTION_DELETE_LINE:
        *refresh = prompt_erase_after_cursor(wayl->prompt);
        if (*refresh)
            matches_update(wayl->matches, wayl->prompt);
        return true;

    case BIND_ACTION_MATCHES_EXECUTE:
        execute_selected(seat, -1);
        return true;

    case BIND_ACTION_MATCHES_EXECUTE_OR_NEXT:
        if (matches_get_total_count(wayl->matches) == 1)
            execute_selected(seat, -1);
        else
            *refresh = matches_selected_next(wayl->matches, true);
        return true;

    case BIND_ACTION_MATCHES_PREV:
        *refresh = matches_selected_prev(wayl->matches, false);
        return true;

    case BIND_ACTION_MATCHES_PREV_WITH_WRAP:
        *refresh = matches_selected_prev(wayl->matches, true);
        return true;

    case BIND_ACTION_MATCHES_PREV_PAGE:
        *refresh = matches_selected_prev_page(wayl->matches);
        return true;

    case BIND_ACTION_MATCHES_NEXT:
        *refresh = matches_selected_next(wayl->matches, false);
        return true;

    case BIND_ACTION_MATCHES_NEXT_WITH_WRAP:
        *refresh = matches_selected_next(wayl->matches, true);
        return true;

    case BIND_ACTION_MATCHES_NEXT_PAGE:
        *refresh = matches_selected_next_page(wayl->matches);
        return true;

    case BIND_ACTION_MATCHES_FIRST:
        *refresh = matches_selected_first(wayl->matches);
        return true;

    case BIND_ACTION_MATCHES_LAST:
        *refresh = matches_selected_last(wayl->matches);
        return true;

    case BIND_ACTION_CUSTOM_1:
    case BIND_ACTION_CUSTOM_2:
    case BIND_ACTION_CUSTOM_3:
    case BIND_ACTION_CUSTOM_4:
    case BIND_ACTION_CUSTOM_5:
    case BIND_ACTION_CUSTOM_6:
    case BIND_ACTION_CUSTOM_7:
    case BIND_ACTION_CUSTOM_8:
    case BIND_ACTION_CUSTOM_9:
    case BIND_ACTION_CUSTOM_10:
    case BIND_ACTION_CUSTOM_11:
    case BIND_ACTION_CUSTOM_12:
    case BIND_ACTION_CUSTOM_13:
    case BIND_ACTION_CUSTOM_14:
    case BIND_ACTION_CUSTOM_15:
    case BIND_ACTION_CUSTOM_16:
    case BIND_ACTION_CUSTOM_17:
    case BIND_ACTION_CUSTOM_18:
    case BIND_ACTION_CUSTOM_19: {
        const size_t idx = action - BIND_ACTION_CUSTOM_1;
        execute_selected(seat, 10 + idx);
        return true;
    }

    case BIND_ACTION_COUNT:
        assert(false);
        return false;
    }

    return false;
}

static bool
symbol_is_keypad(xkb_keysym_t sym)
{
    switch (sym) {
    default:
        return false;

    case XKB_KEY_KP_Space:
    case XKB_KEY_KP_Tab:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_KP_F1:
    case XKB_KEY_KP_F2:
    case XKB_KEY_KP_F3:
    case XKB_KEY_KP_F4:
    case XKB_KEY_KP_Home:
    case XKB_KEY_KP_Left:
    case XKB_KEY_KP_Up:
    case XKB_KEY_KP_Right:
    case XKB_KEY_KP_Down:
    case XKB_KEY_KP_Prior:
    case XKB_KEY_KP_Next:
    case XKB_KEY_KP_End:
    case XKB_KEY_KP_Begin:
    case XKB_KEY_KP_Insert:
    case XKB_KEY_KP_Delete:
    case XKB_KEY_KP_Equal:
    case XKB_KEY_KP_Multiply:
    case XKB_KEY_KP_Add:
    case XKB_KEY_KP_Separator:
    case XKB_KEY_KP_Subtract:
    case XKB_KEY_KP_Decimal:
    case XKB_KEY_KP_Divide:
    case XKB_KEY_KP_0:
    case XKB_KEY_KP_1:
    case XKB_KEY_KP_2:
    case XKB_KEY_KP_3:
    case XKB_KEY_KP_4:
    case XKB_KEY_KP_5:
    case XKB_KEY_KP_6:
    case XKB_KEY_KP_7:
    case XKB_KEY_KP_8:
    case XKB_KEY_KP_9:
        return true;
    }
}

static void
keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
             uint32_t time, uint32_t key, uint32_t state)
{
    /* TODO: these can’t be static - need to reload on keymap changes */
    static bool mod_masks_initialized = false;
    static xkb_mod_mask_t shift = -1;
    static xkb_mod_mask_t ctrl = -1;
    static xkb_mod_mask_t alt = -1;
    static xkb_mod_mask_t super = -1;

    struct seat *seat = data;
    struct wayland *wayl = seat->wayl;

    if (!mod_masks_initialized) {
        mod_masks_initialized = true;

        xkb_mod_index_t shift_idx = xkb_keymap_mod_get_index(
            seat->kbd.xkb_keymap, XKB_MOD_NAME_SHIFT);
        xkb_mod_index_t ctrl_idx = xkb_keymap_mod_get_index(
            seat->kbd.xkb_keymap, XKB_MOD_NAME_CTRL);
        xkb_mod_index_t alt_idx = xkb_keymap_mod_get_index(
            seat->kbd.xkb_keymap, XKB_MOD_NAME_ALT);
        xkb_mod_index_t super_idx = xkb_keymap_mod_get_index(
            seat->kbd.xkb_keymap, XKB_MOD_NAME_LOGO);

        shift = shift_idx != XKB_MOD_INVALID ? 1 << shift_idx : 0;
        ctrl = ctrl_idx != XKB_MOD_INVALID ? 1 << ctrl_idx : 0;
        alt = alt_idx != XKB_MOD_INVALID ? 1 << alt_idx : 0;
        super = super_idx != XKB_MOD_INVALID ? 1 << super_idx : 0;
    }

    if (state == XKB_KEY_UP) {
        repeat_stop(&seat->kbd.repeat, key);
        return;
    }

    key += 8;
    bool should_repeat = xkb_keymap_key_repeats(seat->kbd.xkb_keymap, key);
    xkb_keysym_t sym = xkb_state_key_get_one_sym(seat->kbd.xkb_state, key);

    enum xkb_compose_status compose_status = XKB_COMPOSE_NOTHING;

    if (seat->kbd.xkb_compose_state != NULL) {
        xkb_compose_state_feed(seat->kbd.xkb_compose_state, sym);
        compose_status = xkb_compose_state_get_status(
            seat->kbd.xkb_compose_state);
    }

    if (compose_status == XKB_COMPOSE_COMPOSING) {
        /* TODO: goto maybe_repeat? */
        return;
    }

    xkb_mod_mask_t mods = xkb_state_serialize_mods(
        seat->kbd.xkb_state, XKB_STATE_MODS_EFFECTIVE);
    const xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods2(
        seat->kbd.xkb_state, key, XKB_CONSUMED_MODE_XKB);
    const xkb_mod_mask_t locked = xkb_state_serialize_mods(
        seat->kbd.xkb_state, XKB_STATE_MODS_LOCKED);

    const xkb_mod_mask_t significant = shift | ctrl | alt | super;
    const xkb_mod_mask_t bind_mods
        = mods & significant & ~locked;
    const xkb_mod_mask_t bind_consumed =
        consumed & significant & ~locked;

    const xkb_layout_index_t layout_idx =
        xkb_state_key_get_layout(seat->kbd.xkb_state, key);

    const xkb_keysym_t *raw_syms = NULL;
    const size_t raw_count = xkb_keymap_key_get_syms_by_level(
        seat->kbd.xkb_keymap, key, layout_idx, 0, &raw_syms);

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

    const struct key_binding_set *bindings =
        key_binding_for(wayl->kb_manager, seat);
    assert(bindings != NULL);

    bool refresh = false;
    seat->kbd.serial = serial;
    tll_foreach(bindings->key, it) {
        const struct key_binding *bind = &it->item;

        if (bind->k.sym == sym &&
            bind->mods == (bind_mods & ~bind_consumed) &&
            execute_binding(seat, bind, &refresh))
        {
            if (refresh)
                wayl_refresh(wayl);
            goto maybe_repeat;
        }

        if (bind->mods != bind_mods || bind_mods != (mods & ~locked))
            continue;

        /*
         * Skip raw key codes for keypad keys.
         *
         * The keypad is usually unaffected by the layout. Instead,
         * mapping raw key codes effectively means we’ll ignore
         * NumLock. That is, if we have a key binding for KP_PageUp,
         * it’ll trigger regardless of NumLock setting, and thus
         * making it impossible to use the keypad to enter numericals.
         *
         * See https://codeberg.org/dnkl/fuzzel/issues/192
         */
        if (symbol_is_keypad(bind->k.sym))
            continue;

        for (size_t i = 0; i < raw_count; i++) {
            if (bind->k.sym == raw_syms[i] &&
                execute_binding(seat, bind, &refresh))
            {
                if (refresh)
                    wayl_refresh(wayl);
                goto maybe_repeat;
            }
        }

        tll_foreach(bind->k.key_codes, code) {
            if (code->item == key &&
                execute_binding(seat, bind, &refresh))
            {
                if (refresh)
                    wayl_refresh(wayl);
                goto maybe_repeat;
            }
        }
    }

    if ((bind_mods & ~bind_consumed) != 0)
        goto maybe_repeat;

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

static enum fcft_subpixel
guess_subpixel(const struct wayland *wayl)
{
    if (tll_length(wayl->monitors) == 0)
        return FCFT_SUBPIXEL_DEFAULT;

    return tll_front(wayl->monitors).subpixel;
}

static bool
size_font_using_dpi(const struct wayland *wayl)
{
    switch (wayl->conf->dpi_aware) {
    case DPI_AWARE_NO: return false;
    case DPI_AWARE_YES: return true;

    case DPI_AWARE_AUTO:
        tll_foreach(wayl->monitors, it) {
            const struct monitor *mon = &it->item;
            if (mon->scale > 1)
                return false;
        }

        return true;
    }

    assert(false);
    return false;
}

static bool
reload_font(struct wayland *wayl, float new_dpi, unsigned new_scale)
{
    struct fcft_font *font = NULL;

    bool was_sized_using_dpi = wayl->font_is_sized_by_dpi;
    bool will_size_using_dpi = size_font_using_dpi(wayl);

    bool need_font_reload =
        was_sized_using_dpi != will_size_using_dpi ||
        (will_size_using_dpi
         ? wayl->dpi != new_dpi
         : wayl->scale != new_scale);

    LOG_DBG(
        "font reload: scale: %u -> %u, dpi: %.2f -> %.2f size method: %s -> %s",
        wayl->scale, new_scale, wayl->dpi, new_dpi,
        was_sized_using_dpi ? "DPI" : "scaling factor",
        will_size_using_dpi ? "DPI" : "scaling factor");

    wayl->font_is_sized_by_dpi = will_size_using_dpi;

    if (need_font_reload) {
        char **names = malloc(wayl->font_count * sizeof(names[0]));

        /* Apply font size, possibly scaled using the scaling factor */
        for (size_t i = 0; i < wayl->font_count; i++) {
            const struct font_spec *spec = &wayl->fonts[i];
            const bool use_px_size = spec->px_size > 0;
            const int scale = wayl->font_is_sized_by_dpi ? 1 : new_scale;

            char size[64];
            size_t size_len;
            if (use_px_size) {
                size_len = snprintf(
                    size, sizeof(size), ":pixelsize=%d", spec->px_size * scale);
            } else {
                size_len = snprintf(
                    size, sizeof(size), ":size=%.2f", spec->pt_size * scale);
            }

            size_t len = strlen(spec->pattern) + size_len + 1;
            names[i] = malloc(len);

            strcpy(names[i], spec->pattern);
            strcat(names[i], size);
        }

        /* Font’s DPI */
        float dpi = will_size_using_dpi ? new_dpi : 96.;
        char attrs[256]; snprintf(attrs, sizeof(attrs), "dpi=%.2f", dpi);

        font = fcft_from_name(wayl->font_count, (const char **)names, attrs);

        for (size_t i = 0; i < wayl->font_count; i++)
            free(names[i]);
        free(names);

        if (font == NULL)
            return false;

        bool ret = render_set_font(
            wayl->render, font, new_scale, new_dpi,
            wayl->font_is_sized_by_dpi, &wayl->width, &wayl->height);

        if (wayl->font_reloaded.cb != NULL)
            wayl->font_reloaded.cb(wayl, font, wayl->font_reloaded.data);

        return ret;
    }

    return true;

}

static float
wayl_ppi(const struct wayland *wayl)
{
    /* Use user configured output, if available, otherwise use the
     * "first" output */
    const struct monitor *mon = wayl->monitor != NULL
        ? wayl->monitor
        : (tll_length(wayl->monitors) > 0
           ? &tll_front(wayl->monitors)
           : NULL);

    if (mon != NULL && mon->dpi != 0.)
        return mon->dpi;

    /* No outputs available, return "something" */
    return 96.;
}

static void
update_size(struct wayland *wayl)
{
    const struct monitor *mon = wayl->monitor;
    const int scale = mon != NULL ? mon->scale : guess_scale(wayl);
    const float dpi = wayl_ppi(wayl);

    if (scale == wayl->scale && dpi == wayl->dpi)
        return;

    reload_font(wayl, dpi, scale);

    wayl->scale = scale;
    wayl->dpi = dpi;

    wayl->width /= scale; wayl->width *= scale;
    wayl->height /= scale; wayl->height *= scale;

    zwlr_layer_surface_v1_set_size(
        wayl->layer_surface, wayl->width / scale, wayl->height / scale);

    /* Trigger a 'configure' event, after which we'll have the actual width+height */
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

    /* The *logical* size is affected by the transform */
    switch (mon->transform) {
    case WL_OUTPUT_TRANSFORM_90:
    case WL_OUTPUT_TRANSFORM_270:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: {
        int swap = x_inches;
        x_inches = y_inches;
        y_inches = swap;
        break;
    }

    case WL_OUTPUT_TRANSFORM_NORMAL:
    case WL_OUTPUT_TRANSFORM_180:
    case WL_OUTPUT_TRANSFORM_FLIPPED:
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        break;
    }

    mon->ppi.scaled.x = mon->dim.px_scaled.width / x_inches;
    mon->ppi.scaled.y = mon->dim.px_scaled.height / y_inches;

    float px_diag = sqrt(
        pow(mon->dim.px_scaled.width, 2) +
        pow(mon->dim.px_scaled.height, 2));

    mon->dpi = px_diag / mon->inch * mon->scale;
}

static void
output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
                int32_t physical_width, int32_t physical_height,
                int32_t subpixel, const char *make, const char *model,
                int32_t transform)
{
    struct monitor *mon = data;

    free(mon->make);
    free(mon->model);

    mon->dim.mm.width = physical_width;
    mon->dim.mm.height = physical_height;
    mon->inch = sqrt(pow(mon->dim.mm.width, 2) + pow(mon->dim.mm.height, 2)) * 0.03937008;
    mon->make = make != NULL ? strdup(make) : NULL;
    mon->model = model != NULL ? strdup(model) : NULL;
    mon->subpixel = subpixel;
    mon->transform = transform;

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
        float old_dpi = mon->wayl->dpi;

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

    free(mon->name);
    mon->name = name != NULL ? strdup(name) : NULL;

    if (wayl->conf->output != NULL &&
        mon->name != NULL &&
        strcmp(wayl->conf->output, mon->name) == 0)
    {
        wayl->monitor = mon;
    }
}

static void
xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output,
                              const char *description)
{
    struct monitor *mon = data;
    free(mon->description);
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

        kb_new_for_seat(wayl->kb_manager, wayl->conf, seat);
        wl_seat_add_listener(wl_seat, &seat_listener, seat);
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        const uint32_t required = 2;
        if (!verify_iface_version(interface, version, required))
            return;

        wayl->xdg_output_manager = wl_registry_bind(
            wayl->registry, name, &zxdg_output_manager_v1_interface, required);
    }

    else if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        const uint32_t required = 1;
        if (!verify_iface_version(interface, version, required))
            return;

        wayl->xdg_activation_v1 = wl_registry_bind(
            wayl->registry, name, &xdg_activation_v1_interface, required);
    }
}

static void
monitor_destroy(struct monitor *mon)
{
    if (mon->xdg != NULL)
        zxdg_output_v1_destroy(mon->xdg);
    if (mon->output != NULL)
        wl_output_release(mon->output);
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

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    if (w > 0 && h > 0) {
        if (w * wayl->scale != wayl->width || h * wayl->scale != wayl->height) {
            wayl->width = w * wayl->scale;
            wayl->height = h * wayl->scale;
            wayl_refresh(wayl);
        }
    }
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

    assert(wayl->frame_cb == NULL);
    wayl->frame_cb = wl_surface_frame(wayl->surface);
    wl_callback_add_listener(wayl->frame_cb, &frame_listener, wayl);

    wl_surface_commit(wayl->surface);
}

static void
frame_callback(void *data, struct wl_callback *wl_callback, uint32_t callback_data)
{
    struct wayland *wayl = data;

    assert(wayl->frame_cb == wl_callback);
    wl_callback_destroy(wayl->frame_cb);
    wayl->frame_cb = NULL;

    if (wayl->pending != NULL) {
        commit_buffer(wayl, wayl->pending);
        wayl->pending = NULL;
    }

    if (wayl->render_first_frame_transparent) {
        wayl->render_first_frame_transparent = false;
        wayl_refresh(wayl);
    }
}

void
wayl_refresh(struct wayland *wayl)
{
    struct buffer *buf = shm_get_buffer(wayl->shm, wayl->width, wayl->height);

    pixman_region32_t clip;
    pixman_region32_init_rect(&clip, 0, 0, buf->width, buf->height);
    pixman_image_set_clip_region32(buf->pix, &clip);
    pixman_region32_fini(&clip);

    if (wayl->render_first_frame_transparent) {
        pixman_color_t transparent = {0};
        pixman_image_fill_rectangles(
            PIXMAN_OP_SRC, buf->pix, &transparent,
            1, &(pixman_rectangle16_t){0, 0, wayl->width, wayl->height});
        goto commit;
    }

    render_set_subpixel(wayl->render, wayl->subpixel);

    /* Background + border */
    render_background(wayl->render, buf);

    /* Window content */
    render_prompt(wayl->render, buf, wayl->prompt);
    render_match_list(wayl->render, buf, wayl->prompt, wayl->matches);

#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_surface_flush(buf->cairo_surface);
#endif

commit:
    if (wayl->frame_cb != NULL) {
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

    if (events & EPOLLIN) {
        if (wl_display_read_events(wayl->display) < 0) {
            LOG_ERRNO("failed to read events from the Wayland socket");
            return false;
        }

        while (wl_display_prepare_read(wayl->display) != 0)
            if (wl_display_dispatch_pending(wayl->display) < 0) {
                LOG_ERRNO("failed to dispatch pending Wayland events");
                return false;
            }
    }

    if (events & EPOLLHUP) {
        LOG_WARN("disconnected from Wayland");
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

        enum fcft_subpixel old_subpixel = wayl->subpixel;
        int old_scale = wayl->scale;
        float old_dpi = wayl->dpi;

        wayl->monitor = &it->item;
        wayl->subpixel = wayl->monitor->subpixel;
        update_size(wayl);

        if (wayl->scale != old_scale ||
            wayl->dpi != old_dpi ||
            wayl->subpixel != old_subpixel)
        {
            wayl_refresh(wayl);
        }
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

static bool
font_pattern_to_spec(const char *pattern, struct font_spec *spec)
{
    FcPattern *pat = FcNameParse((const FcChar8 *)pattern);
    if (pat == NULL)
        return false;

    /*
     * First look for user specified {pixel}size option
     * e.g. “font-name:size=12”
     */

    double pt_size = -1.0;
    FcResult have_pt_size = FcPatternGetDouble(pat, FC_SIZE, 0, &pt_size);

    int px_size = -1;
    FcResult have_px_size = FcPatternGetInteger(pat, FC_PIXEL_SIZE, 0, &px_size);

    if (have_pt_size != FcResultMatch && have_px_size != FcResultMatch) {
        /*
         * Apply fontconfig config. Can’t do that until we’ve first
         * checked for a user provided size, since we may end up with
         * both “size” and “pixelsize” being set, and we don’t know
         * which one takes priority.
         */
        FcPattern *pat_copy = FcPatternDuplicate(pat);
        if (pat_copy == NULL ||
            !FcConfigSubstitute(NULL, pat_copy, FcMatchPattern))
        {
            LOG_WARN("%s: failed to do config substitution", pattern);
        } else {
            have_pt_size = FcPatternGetDouble(pat_copy, FC_SIZE, 0, &pt_size);
            have_px_size = FcPatternGetInteger(pat_copy, FC_PIXEL_SIZE, 0, &px_size);
        }

        FcPatternDestroy(pat_copy);

        if (have_pt_size != FcResultMatch && have_px_size != FcResultMatch)
            pt_size = 8.0;
    }

    FcPatternRemove(pat, FC_SIZE, 0);
    FcPatternRemove(pat, FC_PIXEL_SIZE, 0);

    char *stripped_pattern = (char *)FcNameUnparse(pat);
    FcPatternDestroy(pat);

    LOG_DBG("%s: pt-size=%.2f, px-size=%d", stripped_pattern, pt_size, px_size);

    *spec = (struct font_spec){
        .pattern = stripped_pattern,
        .pt_size = pt_size,
        .px_size = px_size
    };

    return true;
}

static void
parse_font_spec(const char *font_spec, size_t *count, struct font_spec **specs)
{
    tll(struct font_spec) fonts = tll_init();

    char *copy = strdup(font_spec);
    for (char *font = strtok(copy, ",");
         font != NULL;
         font = strtok(NULL, ","))
    {
        while (*font != '\0' && isspace(*font))
            font++;

        size_t len = strlen(font);
        while (len > 0 && isspace(font[len - 1]))
            font[--len] = '\0';

        if (font[0] == '\0')
            continue;

        struct font_spec spec;
        if (font_pattern_to_spec(font, &spec))
            tll_push_back(fonts, spec);
    }
    free(copy);

    *count = tll_length(fonts);
    *specs = malloc(*count * sizeof((*specs)[0]));

    struct font_spec *s = *specs;
    tll_foreach(fonts, it) {
        *s = it->item;
        tll_remove(fonts, it);
        s++;
    }
}

struct wayland *
wayl_init(const struct config *conf, struct fdm *fdm,
          struct kb_manager *kb_manager,
          struct render *render, struct prompt *prompt,
          struct matches *matches, font_reloaded_t font_reloaded_cb, void *data)
{
    struct wayland *wayl = malloc(sizeof(*wayl));
    *wayl = (struct wayland){
        .conf = conf,
        .fdm = fdm,
        .kb_manager = kb_manager,
        .render = render,
        .prompt = prompt,
        .matches = matches,
        .status = KEEP_RUNNING,
        .exit_code = !conf->dmenu.enabled ? EXIT_SUCCESS : EXIT_FAILURE,
        .font_reloaded = {
            .cb = font_reloaded_cb,
            .data = data,
        },
    };

    parse_font_spec(conf->font, &wayl->font_count, &wayl->fonts);

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
            "%s: %dx%d+%dx%d@%dHz %s %.2f\" scale=%d PPI=%dx%d (physical) PPI=%dx%d (logical), DPI=%.2f",
            mon->name, mon->dim.px_real.width, mon->dim.px_real.height,
            mon->x, mon->y, (int)round(mon->refresh),
            mon->model != NULL ? mon->model : mon->description,
            mon->inch, mon->scale,
            mon->ppi.real.x, mon->ppi.real.y,
            mon->ppi.scaled.x, mon->ppi.scaled.y, it->item.dpi);
    }

    LOG_DBG("using output: %s",
            wayl->monitor != NULL ? wayl->monitor->name : NULL);

    /*
     * Only do the “first frame is transparent” trick if
     * needed. I.e. if:
     *
     *   - we have more than one monitor (in which case there’s a
     *    chance we guess the scaling factor, or DPI, wrong).
     *
     * and
     *
     *   - the user hasn’t selected a specific output.
     */
    wayl->render_first_frame_transparent =
        tll_length(wayl->monitors) > 1 && wayl->monitor == NULL;

    LOG_DBG("using the first-frame-is-transparent trick: %s",
            wayl->render_first_frame_transparent ? "yes"  :"no");

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
        conf->layer, "launcher");

    if (wayl->layer_surface == NULL) {
        LOG_ERR("failed to create layer shell surface");
        goto out;
    }

    zwlr_layer_surface_v1_set_keyboard_interactivity(wayl->layer_surface, 1);

    zwlr_layer_surface_v1_add_listener(
        wayl->layer_surface, &layer_surface_listener, wayl);

    wayl->subpixel = wayl->monitor != NULL
        ? (enum fcft_subpixel)wayl->monitor->subpixel : guess_subpixel(wayl);
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

    if (wayl->frame_cb != NULL)
        wl_callback_destroy(wayl->frame_cb);

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
    if (wayl->xdg_activation_v1 != NULL)
        xdg_activation_v1_destroy(wayl->xdg_activation_v1);

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
    if (wayl->display != NULL) {
        wayl_flush(wayl);
        wl_display_disconnect(wayl->display);
    }

    for (size_t i = 0; i < wayl->font_count; i++)
        free(wayl->fonts[i].pattern);
    free(wayl->fonts);

    free(wayl);
}

void
wayl_flush(struct wayland *wayl)
{
    wl_display_flush(wayl->display);
}

int
wayl_exit_code(const struct wayland *wayl)
{
    return wayl->exit_code;
}

bool
wayl_update_cache(const struct wayland *wayl)
{
    return wayl->status == EXIT_UPDATE_CACHE;
}
