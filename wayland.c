#include "wayland.h"

#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>

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
#include "kbd-repeater.h"
#include "tllist.h"

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
    struct fdm *fdm;
    struct repeat *repeat;

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
    repeat_stop(wayl->repeat, -1);
    c->status = EXIT;
}

static size_t
prompt_next_char(const struct prompt *prompt)
{
    if (prompt->text[prompt->cursor] == L'\0')
        return prompt->cursor;
    return prompt->cursor + 1;
}

static size_t
prompt_prev_char(const struct prompt *prompt)
{
    if (prompt->cursor == 0)
        return 0;

    return prompt->cursor - 1;
}

static size_t
prompt_prev_word(const struct prompt *prompt)
{
    size_t prev_char = prompt_prev_char(prompt);
    const wchar_t *space = &prompt->text[prev_char];

    /* Ignore initial spaces */
    while (space >= prompt->text && iswspace(*space))
        space--;

    /* Skip non-spaces */
    while (space >= prompt->text && !iswspace(*space))
        space--;

    return space - prompt->text + 1;
}

static size_t
prompt_next_word(const struct prompt *prompt)
{
    const wchar_t *end = prompt->text + wcslen(prompt->text);
    const wchar_t *space = &prompt->text[prompt->cursor];

    /* Ignore initial non-spaces */
    while (space < end && !iswspace(*space))
        space++;

    /* Skip spaces */
    while (space < end && iswspace(*space))
        space++;

    return space - prompt->text;
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
        repeat_stop(c->repeat, key);
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
        c->prompt.cursor = 0;
        refresh(c);
    }

    else if (sym == XKB_KEY_End || (sym == XKB_KEY_e && effective_mods == ctrl)) {
        c->prompt.cursor = wcslen(c->prompt.text);
        refresh(c);
    }

    else if ((sym == XKB_KEY_b && effective_mods == alt) ||
             (sym == XKB_KEY_Left && effective_mods == ctrl)) {
        c->prompt.cursor = prompt_prev_word(&c->prompt);
        refresh(c);
    }

    else if ((sym == XKB_KEY_f && effective_mods == alt) ||
             (sym == XKB_KEY_Right && effective_mods == ctrl)) {

        c->prompt.cursor = prompt_next_word(&c->prompt);
        refresh(c);
    }

    else if ((sym == XKB_KEY_Escape && effective_mods == 0) ||
             (sym == XKB_KEY_g && effective_mods == ctrl)) {
        c->status = EXIT;
    }

    else if ((sym == XKB_KEY_p && effective_mods == ctrl) ||
             (sym == XKB_KEY_Up && effective_mods == 0)) {
        if (c->selected > 0) {
            c->selected--;
            refresh(c);
        }
    }

    else if ((sym == XKB_KEY_n && effective_mods == ctrl) ||
             (sym == XKB_KEY_Down && effective_mods == 0)) {
        if (c->selected + 1 < c->match_count) {
            c->selected++;
            refresh(c);
        }
    }

    else if (sym == XKB_KEY_Tab && effective_mods == 0) {
        if (c->selected + 1 < c->match_count) {
            c->selected++;
            refresh(c);
        } else {
            c->selected = 0;
            refresh(c);
        }
    }

    else if (sym == XKB_KEY_ISO_Left_Tab && effective_mods == 0) {
        if (c->selected > 0) {
            c->selected--;
            refresh(c);
        } else {
            c->selected = c->match_count - 1;
            refresh(c);
        }
    }

    else if ((sym == XKB_KEY_b && effective_mods == ctrl) ||
             (sym == XKB_KEY_Left && effective_mods == 0)) {
        size_t new_cursor = prompt_prev_char(&c->prompt);
        if (new_cursor != c->prompt.cursor) {
            c->prompt.cursor = new_cursor;
            refresh(c);
        }
    }

    else if ((sym == XKB_KEY_f && effective_mods == ctrl) ||
             (sym == XKB_KEY_Right && effective_mods == 0)) {
        size_t new_cursor = prompt_next_char(&c->prompt);
        if (new_cursor != c->prompt.cursor) {
            c->prompt.cursor = new_cursor;
            refresh(c);
        }
    }

    else if ((sym == XKB_KEY_d && effective_mods == ctrl) ||
             (sym == XKB_KEY_Delete && effective_mods == 0)) {
        if (c->prompt.cursor < wcslen(c->prompt.text)) {
            size_t next_char = prompt_next_char(&c->prompt);
            memmove(&c->prompt.text[c->prompt.cursor],
                    &c->prompt.text[next_char],
                    (wcslen(c->prompt.text) - next_char + 1) * sizeof(wchar_t));
            update_matches(c);
            refresh(c);
        }
    }

    else if (sym == XKB_KEY_BackSpace && effective_mods == 0) {
        if (c->prompt.cursor > 0) {
            size_t prev_char = prompt_prev_char(&c->prompt);
            c->prompt.text[prev_char] = L'\0';
            c->prompt.cursor = prev_char;

            update_matches(c);
            refresh(c);
        }
    }

    else if (sym == XKB_KEY_BackSpace && (effective_mods == ctrl ||
                                          effective_mods == alt)) {
        size_t new_cursor = prompt_prev_word(&c->prompt);
        memmove(&c->prompt.text[new_cursor],
                &c->prompt.text[c->prompt.cursor],
                (wcslen(c->prompt.text) - c->prompt.cursor + 1) * sizeof(wchar_t));
        c->prompt.cursor = new_cursor;
        update_matches(c);
        refresh(c);
    }

    else if ((sym == XKB_KEY_d && effective_mods == alt) ||
             (sym == XKB_KEY_Delete && effective_mods == ctrl)) {
        size_t next_word = prompt_next_word(&c->prompt);
        memmove(&c->prompt.text[c->prompt.cursor],
                &c->prompt.text[next_word],
                (wcslen(c->prompt.text) - next_word + 1) * sizeof(wchar_t));
        update_matches(c);
        refresh(c);
    }

    else if (sym == XKB_KEY_k && effective_mods == ctrl) {
        c->prompt.text[c->prompt.cursor] = L'\0';
        update_matches(c);
        refresh(c);
    }

    else if (sym == XKB_KEY_Return && effective_mods == 0) {
        c->status = EXIT;

        struct application *match = c->match_count > 0
            ? c->matches[c->selected].application
            : NULL;

        if (c->dmenu_mode) {
            if (match == NULL) {
                c->status = KEEP_RUNNING;
            } else {
                dmenu_execute(match);
                c->exit_code = EXIT_SUCCESS;
            }
        } else {
            bool success = application_execute(match, &c->prompt);
            c->exit_code = success ? EXIT_SUCCESS : EXIT_FAILURE;

            if (success && match != NULL) {
                c->status = EXIT_UPDATE_CACHE;
                match->count++;
            }
        }
    }

    else if (effective_mods == 0) {
        char buf[128] = {0};
        int count = xkb_state_key_get_utf8(wayl->xkb_state, key, buf, sizeof(buf));

        if (count == 0)
            return;

        const char *b = buf;
        mbstate_t ps = {0};
        size_t wlen = mbsnrtowcs(NULL, &b, count, 0, &ps);

        const size_t new_len = wcslen(c->prompt.text) + wlen + 1;
        wchar_t *new_text = realloc(c->prompt.text, new_len * sizeof(wchar_t));
        if (new_text == NULL)
            return;

        memmove(&new_text[c->prompt.cursor + wlen],
                &new_text[c->prompt.cursor],
                (wcslen(new_text) - c->prompt.cursor + 1) * sizeof(wchar_t));

        b = buf;
        ps = (mbstate_t){0};
        mbsnrtowcs(&new_text[c->prompt.cursor], &b, count, wlen + 1, &ps);

        c->prompt.text = new_text;
        c->prompt.cursor += wlen;

        LOG_DBG("prompt: \"%S\" (cursor=%zu, length=%zu)",
                c->prompt.text, c->prompt.cursor, new_len);

        update_matches(c);
        refresh(c);
    }

    repeat_start(c->repeat, key - 8);

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
    repeat_configure(c->repeat, delay, rate);
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
    wl_keyboard_add_listener(wayl->keyboard, &keyboard_listener, c);
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

static void
handle_global(void *data, struct wl_registry *registry,
              uint32_t name, const char *interface, uint32_t version)
{
    struct wayland *wayl = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wayl->compositor = wl_registry_bind(
            wayl->registry, name, &wl_compositor_interface, 4);
    }

    else if (strcmp(interface, wl_shm_interface.name) == 0) {
        wayl->shm = wl_registry_bind(wayl->registry, name, &wl_shm_interface, 1);
        wl_shm_add_listener(wayl->shm, &shm_listener, &c->wl);
        wl_display_roundtrip(wayl->display);
    }

    else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output = wl_registry_bind(
            wayl->registry, name, &wl_output_interface, 3);

        tll_push_back(wayl->monitors, ((struct monitor){.output = output}));

        struct monitor *mon = &tll_back(wayl->monitors);
        wl_output_add_listener(output, &output_listener, mon);

        /*
         * The "output" interface doesn't give us the monitors'
         * identifiers (e.g. "LVDS-1"). Use the XDG output interface
         * for that.
         */

        assert(wayl->xdg_output_manager != NULL);
        mon->xdg = zxdg_output_manager_v1_get_xdg_output(
            wayl->xdg_output_manager, mon->output);

        zxdg_output_v1_add_listener(mon->xdg, &xdg_output_listener, mon);
        wl_display_roundtrip(wayl->display);
    }

    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        wayl->layer_shell = wl_registry_bind(
            wayl->registry, name, &zwlr_layer_shell_v1_interface, 1);
    }

    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        wayl->seat = wl_registry_bind(wayl->registry, name, &wl_seat_interface, 4);
        wl_seat_add_listener(wayl->seat, &seat_listener, c);
        wl_display_roundtrip(wayl->display);
    }

    else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        wayl->xdg_output_manager = wl_registry_bind(
            wayl->registry, name, &zxdg_output_manager_v1_interface, 2);
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

struct wayland *
wayl_init(struct fdm *fdm, int width, int height)
{
    struct wayland *wayl = malloc(sizeof(*wayl));
    wayl->fdm = fdm;

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

    wl_registry_add_listener(wayl->registry, &registry_listener, &c);
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

    return wayl;

err:
    free(wayl);
}

void
wayl_destroy(struct wayland *wayl)
{
    if (wayl == NULL)
        return;

    free(wayl);
}
