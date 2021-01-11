#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>

#include <locale.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <tllist.h>
#include <fcft/fcft.h>

#define LOG_MODULE "fuzzel"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "application.h"
#include "dmenu.h"
#include "fdm.h"
#include "match.h"
#include "render.h"
#include "shm.h"
#include "version.h"
#include "wayland.h"
#include "xdg.h"

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
        if (wlen == (size_t)-1)
            continue;

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

static void
print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTION]...\n", prog_name);
    printf("\n");
    printf("Options:\n");
    printf("  -o,--output=OUTPUT         output (monitor) to display on (none)\n"
           "  -f,--font=FONT             font name+style in fontconfig format (monospace)\n"
           "  -i,--icon-theme=NAME       icon theme name (\"hicolor\")\n"
           "  -I,--no-icons              do not render any icons\n"
           "  -T,--terminal              terminal command to use when launching 'terminal'\n"
           "                             programs, e.g. \"xterm -e\". Not used in dmenu\n"
           "                             mode (not set)\n"
           "  -l,--lines                 number of matches to show\n"
           "  -w,--width                 window width, in characters (margins and borders\n"
           "                             not included)\n"
           "  -x,--horizontal-pad=PAD    horizontal padding, in pixels\n"
           "  -y,--vertical-pad=PAD      vertical padding, in pixels"
           "  -b,--background-color=HEX  background color (000000ff)\n"
           "  -t,--text-color=HEX        text color (ffffffff)\n"
           "  -m,--match-color=HEX       color of matched substring (cc9393ff)\n"
           "  -s,--selection-color=HEX   background color of selected item (333333ff)\n"
           "  -B,--border-width=INT      width of border, in pixels (1)\n"
           "  -r,--border-radius=INT     amount of corner \"roundness\" (10)\n"
           "  -C,--border-color=HEX      border color (ffffffff)\n"
           "  -d,--dmenu                 dmenu compatibility mode\n"
           "  -R,--no-run-if-empty       exit immediately without showing UI if stdin is \n"
           "                             empty (dmenu mode only)\n"
           "     --line-height=HEIGHT    override line height from font metrics\n"
           "     --letter-spacing=AMOUNT additional letter spacing\n "
           "  -v,--version               show the version number and quit\n");
    printf("\n");
    printf("Colors must be specified as a 32-bit hexadecimal RGBA quadruple.\n");
}

struct icon_reload_context {
    bool icons_enabled;
    const icon_theme_list_t *themes;
    struct application_list *apps;
};

static void
font_reloaded(struct wayland *wayl, struct fcft_font *font, void *data)
{
    struct icon_reload_context *ctx = data;
    if (ctx->icons_enabled)
        icon_reload_application_icons(*ctx->themes, font->height, ctx->apps);
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

int
main(int argc, char *const *argv)
{
    static const struct option longopts[] = {
        {"output"  ,                 required_argument, 0, 'o'},
        {"font",                     required_argument, 0, 'f'},
        {"icon-theme",               required_argument, 0, 'i'},
        {"no-icons",                 no_argument,       0, 'I'},
        {"lines",                    required_argument, 0, 'l'},
        {"width",                    required_argument, 0, 'w'},
        {"horizontal-pad",           required_argument, 0, 'x'},
        {"vertical-pad",             required_argument, 0, 'y'},
        {"background-color",         required_argument, 0, 'b'},
        {"text-color",               required_argument, 0, 't'},
        {"match-color",              required_argument, 0, 'm'},
        {"selection-color",          required_argument, 0, 's'},
        {"border-width",             required_argument, 0, 'B'},
        {"border-radius",            required_argument, 0, 'r'},
        {"border-color",             required_argument, 0, 'C'},
        {"terminal",                 required_argument, 0, 'T'},
        {"dmenu",                    no_argument,       0, 'd'},
        {"no-run-if-empty",          no_argument,       0, 'R'},
        {"line-height",              required_argument, 0, 'H'},
        {"letter-spacing",           required_argument, 0, 'S'},
        {"version",                  no_argument,       0, 'v'},
        {"help",                     no_argument,       0, 'h'},
        {NULL,                       no_argument,       0, 0},
    };

    const char *output_name = NULL;
    const char *font_name = "monospace";
    const char *icon_theme = "hicolor";
    const char *terminal = NULL;
    bool dmenu_mode = false;
    bool no_run_if_empty = false;
    bool icons_enabled = true;

    struct render_options render_options = {
        .lines = 15,
        .chars = 30,
        .border_size = 1u,
        .border_radius = 10u,
        .pad = {.x = {.pt = 20}, .y = {.pt = 8}},
        .background_color = hex_to_rgba(0xfdf6e3dd),
        .border_color = hex_to_rgba(0x002b36ff),
        .text_color = hex_to_rgba(0x657b83ff),
        .match_color = hex_to_rgba(0xcb4b16ff),
        .selection_color = hex_to_rgba(0xeee8d5dd),
        .line_height = {-1, 0.0},
        .letter_spacing = {0},
    };

    while (true) {
        int c = getopt_long(argc, argv, ":o:f:i:Il:w:x:y:b:t:m:s:B:r:C:T:dRvh", longopts, NULL);
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

        case 'I':
            icons_enabled = false;
            break;

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
            if (!pt_or_px_from_string(optarg, &render_options.pad.x))
                return EXIT_FAILURE;
            break;

        case 'y':
            if (!pt_or_px_from_string(optarg, &render_options.pad.y))
                return EXIT_FAILURE;
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

        case 'd':
            dmenu_mode = true;
            break;

        case 'R':
            no_run_if_empty = true;
            break;

        case 'H': { /* line-height */
            if (!pt_or_px_from_string(optarg, &render_options.line_height))
                return EXIT_FAILURE;
            break;
        }

        case 'S': { /* letter-spacing */
            if (!pt_or_px_from_string(optarg, &render_options.letter_spacing))
                return EXIT_FAILURE;
            break;
        }

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

    log_init(LOG_COLORIZE_AUTO, true, LOG_FACILITY_USER, LOG_CLASS_WARNING);

    /* Load applications */
    struct application_list *apps = NULL;
    struct fdm *fdm = NULL;
    struct prompt *prompt = NULL;
    struct matches *matches = NULL;
    struct render *render = NULL;
    struct wayland *wayl = NULL;

    icon_theme_list_t themes = tll_init();

    if (icons_enabled) {
        themes = icon_load_theme(icon_theme);
        if (tll_length(themes) > 0)
            LOG_INFO("theme: %s", tll_front(themes).path);
        else
            LOG_WARN("%s: icon theme not found", icon_theme);
    }

    if ((fdm = fdm_init()) == NULL)
        goto out;


    setlocale(LC_CTYPE, "");

    /* Load applications */
    if ((apps = applications_init()) == NULL)
        goto out;
    if (dmenu_mode) {
        dmenu_load_entries(apps);
        if (no_run_if_empty && apps->count == 0)
            goto out;
    } else
        xdg_find_programs(terminal, apps);
    read_cache(apps);

    if ((render = render_init(&render_options)) == NULL)
        goto out;

    if ((prompt = prompt_init(L"> ")) == NULL)
        goto out;

    if ((matches = matches_init(apps)) == NULL)
        goto out;

    struct icon_reload_context font_reloaded_data = {
        .icons_enabled = icons_enabled,
        .themes = &themes,
        .apps = apps,
    };

    if ((wayl = wayl_init(
             fdm, render, prompt, matches, &render_options,
             dmenu_mode, output_name, font_name,
             &font_reloaded, &font_reloaded_data)) == NULL)
        goto out;

    matches_max_matches_set(matches, render_options.lines);
    matches_update(matches, prompt);
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
    shm_fini();

    wayl_destroy(wayl);
    render_destroy(render);
    matches_destroy(matches);
    prompt_destroy(prompt);
    fdm_destroy(fdm);
    applications_destroy(apps);
    icon_themes_destroy(themes);

#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_debug_reset_static_data();
#endif
    log_deinit();
    return ret;
}
