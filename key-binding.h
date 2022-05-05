#pragma once

#include <xkbcommon/xkbcommon.h>
#include <tllist.h>

#include "config.h"

enum bind_action {
    BIND_ACTION_NONE,
    BIND_ACTION_COUNT,
};

typedef tll(xkb_keycode_t) xkb_keycode_list_t;

struct key_binding {
    int action; /* enum bind_action_* */
    xkb_mod_mask_t mods;

    union {
        struct {
            xkb_keysym_t sym;
            xkb_keycode_list_t key_codes;
        } k;
#if 0
        struct {
            uint32_t button;
            int count;
        } m;
#endif
    };
};
typedef tll(struct key_binding) key_binding_list_t;

struct key_binding_set {
    key_binding_list_t key;
    key_binding_list_t search;
    key_binding_list_t url;
    key_binding_list_t mouse;
    xkb_mod_mask_t selection_overrides;
};
