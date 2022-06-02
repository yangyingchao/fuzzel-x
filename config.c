#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>


#define LOG_MODULE "config"
#define LOG_ENABLE_DBG 0
#include "log.h"

#define ALEN(v) (sizeof(v) / sizeof((v)[0]))

struct context {
    struct config *conf;
    const char *section;
    const char *key;
    const char *value;

    const char *path;
    unsigned lineno;

    bool errors_are_fatal;
};

#define LOG_CONTEXTUAL_ERR(...)                                         \
    log_contextual(ctx, LOG_CLASS_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_CONTEXTUAL_WARN(...) \
    log_contextual(ctx, LOG_CLASS_WARNING, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_CONTEXTUAL_ERRNO(...) \
    log_contextual_errno(ctx, __FILE__, __LINE__, __VA_ARGS__)


static void __attribute__((format(printf, 5, 6)))
log_contextual(struct context *ctx, enum log_class log_class,
               const char *file, int lineno, const char *fmt, ...)
{
    char *formatted_msg = NULL;
    va_list va;
    va_start(va, fmt);
    if (vasprintf(&formatted_msg, fmt, va) < 0) {
        va_end(va);
        return;
    }

    va_end(va);

    bool print_dot = ctx->key != NULL;
    bool print_colon = ctx->value != NULL;

    if (!print_dot)
        ctx->key = "";

    if (!print_colon)
        ctx->value = "";

    log_msg(
        log_class, LOG_MODULE, file, lineno, "%s:%d: [%s]%s%s%s%s: %s",
        ctx->path, ctx->lineno, ctx->section, print_dot ? "." : "",
        ctx->key, print_colon ? ": " : "", ctx->value, formatted_msg);
    free(formatted_msg);
}

struct config_file {
    char *path;       /* Full, absolute, path */
    int fd;           /* FD of file, O_RDONLY */
};

static const char *
get_user_home_dir(void)
{
    const struct passwd *passwd = getpwuid(getuid());
    if (passwd == NULL)
        return NULL;
    return passwd->pw_dir;
}

static struct config_file
open_config(void)
{
    char *path = NULL;
    struct config_file ret = {.path = NULL, .fd = -1};

    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    const char *xdg_config_dirs = getenv("XDG_CONFIG_DIRS");
    const char *home_dir = get_user_home_dir();
    char *xdg_config_dirs_copy = NULL;

    /* First, check XDG_CONFIG_HOME (or .config, if unset) */
    if (xdg_config_home != NULL && xdg_config_home[0] != '\0') {
        if (asprintf(&path, "%s/fuzzel/fuzzel.ini", xdg_config_home) < 0) {
            LOG_ERRNO("failed to build fuzzel.ini path");
            goto done;
        }
    } else if (home_dir != NULL) {
        if (asprintf(&path, "%s/.config/fuzzel/fuzzel.ini", home_dir) < 0) {
            LOG_ERRNO("failed to build fuzzel.ini path");
            goto done;
        }
    }

    if (path != NULL) {
        int fd = open(path, O_RDONLY | O_CLOEXEC);

        if (fd >= 0) {
            ret = (struct config_file) {.path = path, .fd = fd};
            path = NULL;
            goto done;
        }
    }

    xdg_config_dirs_copy = xdg_config_dirs != NULL && xdg_config_dirs[0] != '\0'
        ? strdup(xdg_config_dirs)
        : strdup("/etc/xdg");

    if (xdg_config_dirs_copy == NULL || xdg_config_dirs_copy[0] == '\0')
        goto done;

    for (const char *conf_dir = strtok(xdg_config_dirs_copy, ":");
         conf_dir != NULL;
         conf_dir = strtok(NULL, ":"))
    {
        free(path);
        path = NULL;

        if (asprintf(&path, "%s/fuzzel/fuzzel.ini", conf_dir) < 0) {
            LOG_ERRNO("failed to build fuzzel.ini path");
            goto done;
        }

        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ret = (struct config_file){.path = path, .fd = fd};
            path = NULL;
            goto done;
        }
    }

done:
    free(xdg_config_dirs_copy);
    free(path);
    return ret;
}

static bool
str_to_ulong(const char *s, int base, unsigned long *res)
{
    if (s == NULL)
        return false;

    errno = 0;
    char *end = NULL;

    *res = strtoul(s, &end, base);
    return errno == 0 && *end == '\0';
}

static bool
str_to_uint32(const char *s, int base, uint32_t *res)
{
    unsigned long v;
    bool ret = str_to_ulong(s, base, &v);
    if (!ret)
        return false;
    if (v > UINT32_MAX)
        return false;
    *res = v;
    return true;
}

static bool
value_to_str(struct context *ctx, char **res)
{
    free(*res);
    *res = strdup(ctx->value);
    return true;
}

static bool
value_to_wchars(struct context *ctx, char32_t **res)
{
    char32_t *s = ambstoc32(ctx->value);
    if (s == NULL) {
        LOG_CONTEXTUAL_ERR("not a valie string value");
        return false;
    }

    free(*res);
    *res = s;
    return true;
}

static bool
value_to_bool(struct context *ctx, bool *res)
{
    static const char *const yes[] = {"on", "true", "yes", "1"};
    static const char *const  no[] = {"off", "false", "no", "0"};

    for (size_t i = 0; i < ALEN(yes); i++) {
        if (strcasecmp(ctx->value, yes[i]) == 0) {
            *res = true;
            return true;
        }
    }

    for (size_t i = 0; i < ALEN(no); i++) {
        if (strcasecmp(ctx->value, no[i]) == 0) {
            *res = false;
            return true;
        }
    }

    LOG_CONTEXTUAL_ERR("invalid boolean value");
    return false;
}

static bool
value_to_uint32(struct context *ctx, int base, uint32_t *res)
{
    if (!str_to_uint32(ctx->value, base, res)){
        LOG_CONTEXTUAL_ERR(
            "invalid integer value, or outside range 0-%u", UINT32_MAX);
        return false;
    }
    return true;
}

static bool
value_to_color(struct context *ctx, bool allow_alpha, struct rgba *color)
{
    if (strlen(ctx->value) != 8) {
        LOG_CONTEXTUAL_ERR("not a valid color value");
        return false;
    }

    uint32_t v;
    if (!str_to_uint32(ctx->value, 16, &v)) {
        LOG_CONTEXTUAL_ERR("not a valid color value");
        return false;
    }

    if (!allow_alpha && (v & 0x000000ff) != 0) {
        LOG_CONTEXTUAL_ERR("color value must not have an alpha component");
        return false;
    }

    *color = conf_hex_to_rgba(v);
    return true;
}

static bool
value_to_double(struct context *ctx, float *res)
{
    const char *s = ctx->value;

    if (s == NULL)
        return false;

    errno = 0;
    char *end = NULL;

    *res = strtof(s, &end);
    if (!(errno == 0 && *end == '\0')) {
        LOG_CONTEXTUAL_ERR("invalid decimal value");
        return false;
    }

    return true;
}

static bool
value_to_pt_or_px(struct context *ctx, struct pt_or_px *res)
{
    const char *s = ctx->value;

    size_t len = s != NULL ? strlen(s) : 0;
    if (len >= 2 && s[len - 2] == 'p' && s[len - 1] == 'x') {
        errno = 0;
        char *end = NULL;

        long value = strtol(s, &end, 10);
        if (!(errno == 0 && end == s + len - 2)) {
            LOG_CONTEXTUAL_ERR("invalid px value (must be on the form 12px)");
            return false;
        }
        res->pt = 0;
        res->px = value;
    } else {
        float value;
        if (!value_to_double(ctx, &value))
            return false;
        res->pt = value;
        res->px = 0;
    }

    return true;
}

static bool
value_to_enum(struct context *ctx, const char **value_map, int *res)
{
    size_t str_len = 0;
    size_t count = 0;

    for (; value_map[count] != NULL; count++) {
        if (strcasecmp(value_map[count], ctx->value) == 0) {
            *res = count;
            return true;
        }
        str_len += strlen(value_map[count]);
    }

    const size_t size = str_len + count * 4 + 1;
    char valid_values[512];
    size_t idx = 0;
    assert(size < sizeof(valid_values));

    for (size_t i = 0; i < count; i++)
        idx += snprintf(&valid_values[idx], size - idx, "'%s', ", value_map[i]);

    if (count > 0)
        valid_values[idx - 2] = '\0';

    LOG_CONTEXTUAL_ERR("not one of %s", valid_values);
    *res = -1;
    return false;
}


static bool parse_config_file(
    FILE *f, struct config *conf, const char *path, bool errors_are_fatal);

static bool
parse_section_main(struct context *ctx)
{
    struct config *conf = ctx->conf;
    const char *key = ctx->key;
    const char *value = ctx->value;

    if (strcmp(key, "output") == 0)
        return value_to_str(ctx, &conf->output);

    else if (strcmp(key, "font") == 0)
        return value_to_str(ctx, &conf->font);

    else if (strcmp(key, "dpi-aware") == 0) {
        if (strcmp(value, "auto") == 0)
            conf->dpi_aware = DPI_AWARE_AUTO;
        else {
            bool value;
            if (!value_to_bool(ctx, &value))
                return false;
            conf->dpi_aware = value ? DPI_AWARE_YES : DPI_AWARE_NO;
        }
        return true;
    }

    else if (strcmp(key, "prompt") == 0)
        return value_to_wchars(ctx, &conf->prompt);

    else if (strcmp(key, "icon-theme") == 0)
        return value_to_str(ctx, &conf->icon_theme);

    else if (strcmp(key, "icons-enabled") == 0)
        return value_to_bool(ctx, &conf->icons_enabled);

    else if (strcmp(key, "fields") == 0) {
        _Static_assert(sizeof(conf->match_fields) == sizeof(int),
            "enum is not 32-bit");

        enum match_fields match_fields = 0;

        char *copy = strdup(value);
        for (const char *field = strtok(copy, ",");
             field != NULL;
             field = strtok(NULL, ","))
        {
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

            enum match_fields field_value = 0;

            for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                if (strcmp(field, map[i].name) == 0) {
                    field_value = map[i].value;
                    break;
                }
            }

            if (field_value == 0) {
                LOG_CONTEXTUAL_ERR(
                    "invalid field name \"%s\", "
                    "must be one of: "
                    "\"filename\", \"name\", \"generic\", \"exec\", "
                    "\"categories\", \"keywords\", \"comment\"",
                    field);
                free(copy);
                return false;
            }

            match_fields |= field_value;
        }

        conf->match_fields = match_fields;
        free(copy);
        return true;
    }

    else if (strcmp(key, "password-character") == 0) {
        char32_t *password_chars = ambstoc32(value);
        if (password_chars == NULL || c32len(password_chars) != 1) {
            LOG_CONTEXTUAL_ERR("invalid password character");
            free(password_chars);
            return false;
        }

        conf->password = password_chars[0];
        free(password_chars);
        return true;
    }

    else if (strcmp(key, "fuzzy") == 0)
        return value_to_bool(ctx, &conf->fuzzy.enabled);

    else if (strcmp(key, "show-actions") == 0)
        return value_to_bool(ctx, &conf->actions_enabled);

    else if (strcmp(key, "terminal") == 0)
        return value_to_str(ctx, &conf->terminal);

    else if (strcmp(key, "launch-prefix") == 0)
        return value_to_str(ctx, &conf->launch_prefix);

    else if (strcmp(key, "lines") == 0)
        return value_to_uint32(ctx, 10, &conf->lines);

    else if (strcmp(key, "width") == 0)
        return value_to_uint32(ctx, 10, &conf->chars);

    else if (strcmp(key, "horizontal-pad") == 0)
        return value_to_uint32(ctx, 10, &conf->pad.x);

    else if (strcmp(key, "vertical-pad") == 0)
        return value_to_uint32(ctx, 10, &conf->pad.y);

    else if (strcmp(key, "inner-pad") == 0)
        return value_to_uint32(ctx, 10, &conf->pad.inner);

    else if (strcmp(key, "line-height") == 0)
        return value_to_pt_or_px(ctx, &conf->line_height);

    else if (strcmp(key, "letter-spacing") == 0)
        return value_to_pt_or_px(ctx, &conf->letter_spacing);

    else
        LOG_CONTEXTUAL_ERR("not a valid option: %s", key);

    return false;
}

static bool
parse_section_colors(struct context *ctx)
{
    struct config *conf = ctx->conf;
    const char *key = ctx->key;

    if (strcmp(key, "background") == 0)
        return value_to_color(ctx, true, &conf->colors.background);

    else if (strcmp(key, "text") == 0)
        return value_to_color(ctx, true, &conf->colors.text);

    else if (strcmp(key, "match") == 0)
        return value_to_color(ctx, true, &conf->colors.match);

    else if (strcmp(key, "selection") == 0)
        return value_to_color(ctx, true, &conf->colors.selection);

    else if (strcmp(key, "selection-text") == 0)
        return value_to_color(ctx, true, &conf->colors.selection_text);

    else if (strcmp(key, "border") == 0)
        return value_to_color(ctx, true, &conf->colors.border);

    else
        LOG_CONTEXTUAL_ERR("not a valid option: %s", key);

    return false;
}

static bool
parse_section_border(struct context *ctx)
{
    struct config *conf = ctx->conf;
    const char *key = ctx->key;

    if (strcmp(key, "width") == 0)
        return value_to_uint32(ctx, 10, &conf->border.size);

    else if (strcmp(key, "radius") == 0)
        return value_to_uint32(ctx, 10, &conf->border.radius);

    else
        LOG_CONTEXTUAL_ERR("not a valid option: %s", key);

    return false;
}

static bool
parse_section_dmenu(struct context *ctx)
{
    struct config *conf = ctx->conf;
    const char *key = ctx->key;

    if (strcmp(key, "mode") == 0) {
        _Static_assert(sizeof(conf->dmenu.mode) == sizeof(int),
            "enum is not 32-bit");

        return value_to_enum(
            ctx,
            (const char *[]){"text", "index", NULL},
            (int *)&conf->dmenu.mode);
    }

    else if (strcmp(key, "exit-immediately-if-empty") == 0)
        return value_to_bool(ctx, &conf->dmenu.exit_immediately_if_empty);

    else
        LOG_CONTEXTUAL_ERR("not a valid option: %s", key);

    return false;
}

enum section {
    SECTION_MAIN,
    SECTION_COLORS,
    SECTION_BORDER,
    SECTION_DMENU,
    SECTION_COUNT,
};

/* Function pointer, called for each key/value line */
typedef bool (*parser_fun_t)(struct context *ctx);

static const struct {
    parser_fun_t fun;
    const char *name;
} section_info[] = {
    [SECTION_MAIN] =   {&parse_section_main, "main"},
    [SECTION_COLORS] = {&parse_section_colors, "colors"},
    [SECTION_BORDER] = {&parse_section_border, "border"},
    [SECTION_DMENU] =  {&parse_section_dmenu, "dmenu"},
};

static enum section
str_to_section(const char *str)
{
    for (enum section section = SECTION_MAIN; section < SECTION_COUNT; ++section) {
        if (strcmp(str, section_info[section].name) == 0)
            return section;
    }
    return SECTION_COUNT;
}

static bool
parse_key_value(char *kv, const char **section, const char **key, const char **value)
{
    bool section_is_needed = section != NULL;

    /* Strip leading whitespace */
    while (isspace(kv[0]))
        ++kv;

    if (section_is_needed)
        *section = "main";

    if (kv[0] == '=')
        return false;

    *key = kv;
    *value = NULL;

    size_t kvlen = strlen(kv);

    /* Strip trailing whitespace */
    while (isspace(kv[kvlen - 1]))
        kvlen--;
    kv[kvlen] = '\0';

    for (size_t i = 0; i < kvlen; ++i) {
        if (kv[i] == '.' && section_is_needed) {
            section_is_needed = false;
            *section = kv;
            kv[i] = '\0';
            if (i == kvlen - 1 || kv[i + 1] == '=') {
                *key = NULL;
                return false;
            }
            *key = &kv[i + 1];
        } else if (kv[i] == '=') {
            kv[i] = '\0';
            if (i != kvlen - 1)
                *value = &kv[i + 1];
            break;
        }
    }

    if (*value == NULL)
        return false;

    /* Strip trailing whitespace from key (leading stripped earlier) */
    {
        assert(!isspace(*key[0]));

        char *end = (char *)*key + strlen(*key) - 1;
        while (isspace(end[0]))
            end--;
        end[1] = '\0';
    }

    /* Strip leading whitespace from value (trailing stripped earlier) */
    while (isspace(*value[0]))
        ++*value;

    return true;
}

static bool
parse_config_file(FILE *f, struct config *conf, const char *path,
                  bool errors_are_fatal)
{
    enum section section = SECTION_MAIN;

    char *_line = NULL;
    size_t count = 0;
    bool ret = true;

#define error_or_continue()                     \
    {                                           \
        if (errors_are_fatal) {                 \
            ret = false;                        \
            goto done;                          \
        } else                                  \
            continue;                           \
    }

    char *section_name = strdup("main");

    struct context context = {
        .conf = conf,
        .section = section_name,
        .path = path,
        .lineno = 0,
        .errors_are_fatal = errors_are_fatal,
    };
    struct context *ctx = &context;  /* For LOG_AND_*() */

    errno = 0;
    ssize_t len;

    while ((len = getline(&_line, &count, f)) != -1) {
        context.key = NULL;
        context.value = NULL;
        context.lineno++;

        char *line = _line;

        /* Strip leading whitespace */
        while (isspace(line[0])) {
            line++;
            len--;
        }

        /* Empty line, or comment */
        if (line[0] == '\0' || line[0] == '#')
            continue;

        /* Strip the trailing newline - may be absent on the last line */
        if (line[len - 1] == '\n')
            line[--len] = '\0';

        /* Split up into key/value pair + trailing comment separated by blank */
        char *key_value = line;
        char *kv_trailing = &line[len - 1];
        char *comment = &line[1];
        while (comment[1] != '\0') {
            if (isblank(comment[0]) && comment[1] == '#') {
                comment[1] = '\0'; /* Terminate key/value pair */
                kv_trailing = comment++;
                break;
            }
            comment++;
        }
        comment++;

        /* Strip trailing whitespace */
        while (isspace(kv_trailing[0]))
            kv_trailing--;
        kv_trailing[1] = '\0';

        /* Check for new section */
        if (key_value[0] == '[') {
            key_value++;

            if (key_value[0] == ']') {
                LOG_CONTEXTUAL_ERR("empty section name");
                section = SECTION_COUNT;
                error_or_continue();
            }

            char *end = strchr(key_value, ']');

            if (end == NULL) {
                context.section = key_value;
                LOG_CONTEXTUAL_ERR("syntax error: no closing ']'");
                context.section = section_name;
                section = SECTION_COUNT;
                error_or_continue();
            }

            end[0] = '\0';

            if (end[1] != '\0') {
                context.section = key_value;
                LOG_CONTEXTUAL_ERR("section declaration contains trailing "
                                   "characters");
                context.section = section_name;
                section = SECTION_COUNT;
                error_or_continue();
            }

            section = str_to_section(key_value);
            if (section == SECTION_COUNT) {
                context.section = key_value;
                LOG_CONTEXTUAL_ERR("invalid section name: %s", key_value);
                context.section = section_name;
                error_or_continue();
            }

            free(section_name);
            section_name = strdup(key_value);
            context.section = section_name;

            /* Process next line */
            continue;
        }

        if (section >= SECTION_COUNT) {
            /* Last section name was invalid; ignore all keys in it */
            continue;
        }

        if (!parse_key_value(key_value, NULL, &context.key, &context.value)) {
            LOG_CONTEXTUAL_ERR("syntax error: key/value pair has no %s",
                               context.key == NULL ? "key" : "value");
            error_or_continue();
        }

        LOG_DBG("section=%s, key='%s', value='%s', comment='%s'",
                section_info[section].name, context.key, context.value, comment);

        assert(section >= 0 && section < SECTION_COUNT);

        parser_fun_t section_parser = section_info[section].fun;
        assert(section_parser != NULL);

        if (!section_parser(ctx))
            error_or_continue();
    }

    if (errno != 0) {
        LOG_ERRNO("failed to read from configuration");
        if (errors_are_fatal)
            ret = false;
    }

done:
    free(section_name);
    free(_line);
    return ret;
}

static bool
overrides_apply(struct config *conf, const config_override_t *overrides,
                bool errors_are_fatal)
{
    if (overrides == NULL)
        return true;

    struct context context = {
        .conf = conf,
        .path = "override",
        .lineno = 0,
        .errors_are_fatal = errors_are_fatal,
    };
    struct context *ctx = &context;

    tll_foreach(*overrides, it) {
        context.lineno++;

        if (!parse_key_value(
                it->item, &context.section, &context.key, &context.value))
        {
            LOG_CONTEXTUAL_ERR("syntax error: key/value pair has no %s",
                               context.key == NULL ? "key" : "value");
            if (errors_are_fatal)
                return false;
            continue;
        }

        if (context.section[0] == '\0') {
            LOG_CONTEXTUAL_ERR("empty section name");
            if (errors_are_fatal)
                return false;
            continue;
        }

        enum section section = str_to_section(context.section);
        if (section == SECTION_COUNT) {
            LOG_CONTEXTUAL_ERR("invalid section name: %s", context.section);
            if (errors_are_fatal)
                return false;
            continue;
        }
        parser_fun_t section_parser = section_info[section].fun;
        assert(section_parser != NULL);

        if (!section_parser(ctx)) {
            if (errors_are_fatal)
                return false;
            continue;
        }
    }

#if 0
    return
        resolve_key_binding_collisions(
            conf, section_info[SECTION_KEY_BINDINGS].name,
            binding_action_map, &conf->bindings.key, KEY_BINDING) &&
        resolve_key_binding_collisions(
            conf, section_info[SECTION_SEARCH_BINDINGS].name,
            search_binding_action_map, &conf->bindings.search, KEY_BINDING) &&
        resolve_key_binding_collisions(
            conf, section_info[SECTION_URL_BINDINGS].name,
            url_binding_action_map, &conf->bindings.url, KEY_BINDING) &&
        resolve_key_binding_collisions(
            conf, section_info[SECTION_MOUSE_BINDINGS].name,
            binding_action_map, &conf->bindings.mouse, MOUSE_BINDING);
#endif
    return true;
}

bool
config_load(struct config *conf, const char *conf_path,
            const config_override_t *overrides, bool errors_are_fatal)
{
    bool ret = !errors_are_fatal;
    *conf = (struct config) {
        .output = NULL,
        .prompt = c32dup(U"> "),
        .password = U'\0',
        .match_fields = MATCH_FILENAME | MATCH_NAME | MATCH_GENERIC,
        .terminal = NULL,
        .launch_prefix = NULL,
        .font = strdup("monospace"),
        .dpi_aware = DPI_AWARE_AUTO,
        .icons_enabled = true,
        .icon_theme = strdup("hicolor"),
        .actions_enabled = false,
        .fuzzy = {
            .min_length = 3,
            .max_length_discrepancy = 2,
            .max_distance = 1,
            .enabled = true,
        },
        .dmenu = {
            .enabled = false,
            .mode = DMENU_MODE_TEXT,
            .exit_immediately_if_empty = false,
        },
        .lines = 15,
        .chars = 30,
        .pad = {
            .x = 40,
            .y = 8,
            .inner = 0,
        },
        .colors = {
            .background = conf_hex_to_rgba(0xfdf6e3dd),
            .border = conf_hex_to_rgba(0x002b36ff),
            .text = conf_hex_to_rgba(0x657b83ff),
            .match = conf_hex_to_rgba(0xcb4b16ff),
            .selection = conf_hex_to_rgba(0xeee8d5dd),
            .selection_text = conf_hex_to_rgba(0x657b83ff),
        },
        .border = {
            .size = 1u,
            .radius = 10u,
        },
        .line_height = {-1, 0.0},
        .letter_spacing = {0},
    };

    struct config_file conf_file = {.path = NULL, .fd = -1};
    if (conf_path != NULL) {
        int fd = open(conf_path, O_RDONLY);
        if (fd < 0) {
            LOG_ERRNO("%s: failed to open", conf_path);
            goto out;
        }

        conf_file.path = strdup(conf_path);
        conf_file.fd = fd;
    } else {
        conf_file = open_config();
        if (conf_file.fd < 0) {
            LOG_WARN("no configuration found, using defaults");
            goto out;
        }
    }

    LOG_INFO("loading configuration from %s", conf_file.path);

    FILE *f = fdopen(conf_file.fd, "r");
    if (f == NULL) {
        LOG_ERRNO("%s: failed to open", conf_file.path);
        goto out;
    }

    if (!parse_config_file(f, conf, conf_file.path, errors_are_fatal) ||
        !overrides_apply(conf, overrides, errors_are_fatal))
    {
        ret = !errors_are_fatal;
    } else
        ret = true;

    fclose(f);

out:

    free(conf_file.path);
    if (conf_file.fd >= 0)
        close(conf_file.fd);

    return ret;
}

void
config_free(struct config *conf)
{
    free(conf->output);
    free(conf->prompt);
    free(conf->terminal);
    free(conf->launch_prefix);
    free(conf->font);
    free(conf->icon_theme);
}

struct rgba
conf_hex_to_rgba(uint32_t color)
{
    return (struct rgba){
        .r = (double)((color >> 24) & 0xff) / 255.0,
        .g = (double)((color >> 16) & 0xff) / 255.0,
        .b = (double)((color >>  8) & 0xff) / 255.0,
        .a = (double)((color >>  0) & 0xff) / 255.0,
    };
}
