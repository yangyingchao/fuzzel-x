#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <char32.h>

#include <wlr-layer-shell-unstable-v1.h>

#include <tllist.h>
#include <xkbcommon/xkbcommon.h>

#define DEFINE_LIST(type) \
    type##_list {         \
        size_t count;     \
        type *arr;        \
    }

struct rgba {double r; double g; double b; double a;};
struct pt_or_px {int px; float pt;};

enum dpi_aware {
    DPI_AWARE_AUTO,
    DPI_AWARE_YES,
    DPI_AWARE_NO,
};

enum dmenu_mode {
    DMENU_MODE_TEXT,
    DMENU_MODE_INDEX,
};

enum match_mode {
    MATCH_MODE_EXACT,
    MATCH_MODE_FZF,
    MATCH_MODE_FUZZY,
};

enum match_fields {
    MATCH_NAME =       0x01,
    MATCH_FILENAME =   0x02,
    MATCH_GENERIC =    0x04,
    MATCH_EXEC =       0x08,
    MATCH_COMMENT =    0x10,
    MATCH_KEYWORDS =   0x20,
    MATCH_CATEGORIES = 0x40,

    MATCH_END = MATCH_COMMENT + 1,
};

enum anchors {
    ANCHOR_TOP_LEFT =     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
    ANCHOR_TOP =          ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
    ANCHOR_TOP_RIGHT =    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    ANCHOR_LEFT  =        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
    ANCHOR_CENTER =       0,
    ANCHOR_RIGHT =        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    ANCHOR_BOTTOM_LEFT =  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
    ANCHOR_BOTTOM =       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
    ANCHOR_BOTTOM_RIGHT = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
};

struct anchors_map {
    const char *name;
    enum anchors value;
};

extern const struct anchors_map anchors_map[];

struct config_key_modifiers {
    bool shift;
    bool alt;
    bool ctrl;
    bool super;
};

struct config_key_binding {
    int action;  /* One of the various bind_action_* enums from wayland.h */
    struct config_key_modifiers modifiers;
    union {
        /* Key bindings */
        struct {
            xkb_keysym_t sym;
        } k;

#if 0
        /* Mouse bindings */
        struct {
            int button;
            int count;
        } m;
#endif
    };

    /* For error messages in collision handling */
    const char *path;
    int lineno;
};
DEFINE_LIST(struct config_key_binding);

struct config {
    char *output;
    char32_t *prompt;
    char32_t *placeholder;
    char32_t *search_text;
    bool prompt_only;
    enum match_fields match_fields;

    struct {
        char32_t character;
        bool enabled;
        bool character_set;
    } password_mode;

    char *terminal;
    char *launch_prefix;

    char *font;
    bool use_bold;
    enum dpi_aware dpi_aware;

    uint16_t render_worker_count;
    uint16_t match_worker_count;

    bool filter_desktop;

    bool icons_enabled;
    char *icon_theme;

    bool hide_when_prompt_empty;

    bool actions_enabled;

    struct config_key_binding_list key_bindings;

    enum match_mode match_mode;
    bool sort_result;
    bool match_counter;

    uint32_t delayed_filter_ms;
    uint32_t delayed_filter_limit;

    struct {
        size_t min_length;
        size_t max_length_discrepancy;
        size_t max_distance;
    } fuzzy;

    struct {
        bool enabled;
        enum dmenu_mode mode;
        bool exit_immediately_if_empty;
        char delim;
    } dmenu;

    enum anchors anchor;

    struct {
        unsigned x;
        unsigned y;
    } margin;

    unsigned lines;
    unsigned chars;
    unsigned tabs;  /* Tab stop every number of #spaces */

    struct {
        unsigned x;
        unsigned y;
        unsigned inner;
    } pad;

    struct {
        struct rgba background;
        struct rgba border;
        struct rgba text;
        struct rgba prompt;
        struct rgba input;
        struct rgba match;
        struct rgba selection;
        struct rgba selection_text;
        struct rgba selection_match;
        struct rgba counter;
        struct rgba placeholder;
    } colors;

    struct {
        unsigned size;
        unsigned radius;
    } border;

    float image_size_ratio;

    struct pt_or_px line_height;
    struct pt_or_px letter_spacing;

    enum zwlr_layer_shell_v1_layer layer;
    bool exit_on_kb_focus_loss;

    bool list_executables_in_path;
    char *cache_path;
};

typedef tll(char *) config_override_t;

bool config_load(
    struct config *conf, const char *path, const config_override_t *overrides,
    bool errors_are_fatal);
void config_free(struct config *conf);

struct rgba conf_hex_to_rgba(uint32_t color);
