#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include <locale.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

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
        if (errno != ENOENT)
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

        if (title == NULL || count_str == NULL) {
            LOG_ERR("invalid cache entry (cache corrupt?): %s", line);
            continue;
        }

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
    printf("  -o,--output=OUTPUT             output (monitor) to display on (none)\n"
           "  -f,--font=FONT                 font name and style, in FontConfig format\n"
           "                                 (monospace)\n"
           "  -i,--icon-theme=NAME           icon theme name (\"hicolor\")\n"
           "  -I,--no-icons                  do not render any icons\n"
           "  -F,--fields=FIELDS             comma separated list of XDG Desktop entry fields to match\n"
           "  -T,--terminal                  terminal command to use when launching\n"
           "                                 'terminal' programs, e.g. \"xterm -e\".\n"
           "                                 Not used in dmenu mode (not set)\n"
           "  -l,--lines                     number of matches to show\n"
           "  -w,--width                     window width, in characters (margins and\n"
           "                                 borders not included)\n"
           "  -x,--horizontal-pad=PAD        horizontal padding, in pixels (40)\n"
           "  -y,--vertical-pad=PAD          vertical padding, in pixels (8)\n"
           "  -p,--inner-pad=PAD             vertical padding between prompt and match list,\n"
           "                                 in pixels (0)\n"
           "  -b,--background-color=HEX      background color (000000ff)\n"
           "  -t,--text-color=HEX            text color (ffffffff)\n"
           "  -m,--match-color=HEX           color of matched substring (cc9393ff)\n"
           "  -s,--selection-color=HEX       background color of selected item (333333ff)\n"
           "  -S,--selection-text-color=HEX  text color of selected item (ffffffff)\n"
           "  -B,--border-width=INT          width of border, in pixels (1)\n"
           "  -r,--border-radius=INT         amount of corner \"roundness\" (10)\n"
           "  -C,--border-color=HEX          border color (ffffffff)\n"
           "  -d,--dmenu                     dmenu compatibility mode\n"
           "  -R,--no-run-if-empty           exit immediately without showing UI if stdin\n"
           "                                 is empty (dmenu mode only)\n"
           "     --line-height=HEIGHT        override line height from font metrics\n"
           "     --letter-spacing=AMOUNT     additional letter spacing\n"
           "     --launch-prefix=COMMAND     prefix to add before argv of executed program\n"
           "  -v,--version                   show the version number and quit\n");
    printf("\n");
    printf("All colors are RGBA - i.e. 8-digit hex values, without prefix.\n");
}

struct font_reload_context {
    bool icons_enabled;
    const icon_theme_list_t *themes;
    struct application_list *apps;
};

static void
font_reloaded(struct wayland *wayl, struct fcft_font *font, void *data)
{
    struct font_reload_context *ctx = data;
    if (ctx->icons_enabled)
        icon_reload_application_icons(*ctx->themes, font->height, ctx->apps);
    applications_flush_text_run_cache(ctx->apps);
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

static bool
have_another_fuzzel_instance(void)
{
    bool ret = false;

    pid_t my_pid = getpid();
    int proc_fd = -1;
    DIR *proc_dir = NULL;

    if ((proc_fd = open("/proc", O_RDONLY)) < 0) {
        LOG_ERRNO("/proc: failed to open");
        goto out;
    }

    if ((proc_dir = opendir("/proc")) == NULL) {
        LOG_ERRNO("/proc: failed to open");
        goto out;
    }

    for (struct dirent *e = readdir(proc_dir);
         e != NULL && !ret;
         e = readdir(proc_dir))
    {
        if (e->d_type != DT_DIR)
            continue;

        errno = 0;
        char *end = NULL;

        unsigned long pid = strtoul(e->d_name, &end, 10);
        if (errno != 0 || *end != '\0')
            continue;

        if (pid == my_pid)
            continue;

        int pid_fd = openat(proc_fd, e->d_name, O_RDONLY);
        if (pid_fd < 0)
            continue;

        int comm_fd = openat(pid_fd, "comm", O_RDONLY);
        if (comm_fd >= 0) {
            char comm[64] = {};
            ssize_t r = read(comm_fd, comm, sizeof(comm) - 1);

            for (; r > 0 && comm[r - 1] == '\n'; r--)
                comm[r - 1] = '\0';

            if (strcmp(comm, "fuzzel") == 0)
                ret = true;

            close(comm_fd);
        }

        close(pid_fd);
    }

out:
    if (proc_dir != NULL)
        closedir(proc_dir);
    if (proc_fd >= 0)
        close(proc_fd);
    return ret;
}

int
main(int argc, char *const *argv)
{
    #define OPT_LETTER_SPACING 256
    #define OPT_LAUNCH_PREFIX  257

    static const struct option longopts[] = {
        {"output"  ,             required_argument, 0, 'o'},
        {"font",                 required_argument, 0, 'f'},
        {"dpi-aware",            required_argument, 0, 'D'},
        {"icon-theme",           required_argument, 0, 'i'},
        {"no-icons",             no_argument,       0, 'I'},
        {"fields",               required_argument, 0, 'F'},
        {"lines",                required_argument, 0, 'l'},
        {"width",                required_argument, 0, 'w'},
        {"horizontal-pad",       required_argument, 0, 'x'},
        {"vertical-pad",         required_argument, 0, 'y'},
        {"inner-pad",            required_argument, 0, 'p'},
        {"background-color",     required_argument, 0, 'b'},
        {"text-color",           required_argument, 0, 't'},
        {"match-color",          required_argument, 0, 'm'},
        {"selection-color",      required_argument, 0, 's'},
        {"selection-text-color", required_argument, 0, 'S'},
        {"border-width",         required_argument, 0, 'B'},
        {"border-radius",        required_argument, 0, 'r'},
        {"border-color",         required_argument, 0, 'C'},
        {"prompt",               required_argument, 0, 'P'},
        {"terminal",             required_argument, 0, 'T'},
        {"dmenu",                no_argument,       0, 'd'},
        {"no-run-if-empty",      no_argument,       0, 'R'},
        {"line-height",          required_argument, 0, 'H'},
        {"letter-spacing",       required_argument, 0, OPT_LETTER_SPACING},
        {"launch-prefix",        required_argument, 0, OPT_LAUNCH_PREFIX},
        {"version",              no_argument,       0, 'v'},
        {"help",                 no_argument,       0, 'h'},
        {NULL,                   no_argument,       0, 0},
    };

    enum dpi_aware dpi_aware = DPI_AWARE_AUTO;
    const char *output_name = NULL;
    const char *font_name = "monospace";
    const char *icon_theme = "hicolor";
    const char *terminal = NULL;
    const wchar_t *prompt_content = L"> ";
    wchar_t *prompt_allocated = NULL;
    bool dmenu_mode = false;
    bool no_run_if_empty = false;
    bool icons_enabled = true;
    const char *launch_prefix = NULL;

    enum match_fields match_fields =
        MATCH_FILENAME | MATCH_NAME | MATCH_GENERIC;

    struct render_options render_options = {
        .lines = 15,
        .chars = 30,
        .border_size = 1u,
        .border_radius = 10u,
        .pad = {.x = 40, .y = 8, .inner = 0},
        .background_color = hex_to_rgba(0xfdf6e3dd),
        .border_color = hex_to_rgba(0x002b36ff),
        .text_color = hex_to_rgba(0x657b83ff),
        .match_color = hex_to_rgba(0xcb4b16ff),
        .selection_color = hex_to_rgba(0xeee8d5dd),
        .selection_text_color = hex_to_rgba(0x657b83ff),
        .line_height = {-1, 0.0},  /* Use font metrics */
        .letter_spacing = {0},
    };

    setlocale(LC_CTYPE, "");

    while (true) {
        int c = getopt_long(argc, argv, ":o:f:D:i:IF:l:w:x:y:p:P:b:t:m:s:S:B:r:C:T:dRvh", longopts, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'o':
            output_name = optarg;
            break;

        case 'f':
            font_name = optarg;
            break;

        case 'D':
            if (strcmp(optarg, "auto") == 0)
                dpi_aware = DPI_AWARE_AUTO;
            else if (strcmp(optarg, "no") == 0)
                dpi_aware = DPI_AWARE_NO;
            else if (strcmp(optarg, "yes") == 0)
                dpi_aware = DPI_AWARE_YES;
            else {
                fprintf(
                    stderr,
                    "%s: invalid value for dpi-aware: "
                    "must be one of 'auto', 'no', or 'yes'\n",
                    optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'i':
            icon_theme = optarg;
            break;

        case 'I':
            icons_enabled = false;
            break;

        case 'F': {
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

            match_fields = 0;
            for (const char *f = strtok(optarg, ", ");
                 f != NULL;
                 f = strtok(NULL, ", "))
            {
                enum match_fields field = 0;

                for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                    if (strcmp(f, map[i].name) == 0) {
                        field = map[i].value;
                        break;
                    }
                }

                if (field > 0)
                    match_fields |= field;
                else {
                    char valid_names[128] = {0};
                    int idx = 0;
                    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                        idx += snprintf(
                            &valid_names[idx], sizeof(valid_names) - idx,
                            "'%s', ", map[i].name);
                    }

                    valid_names[idx - 2] = '\0';

                    fprintf(
                        stderr,
                        "%s: invalid field name: must be one of %s\n",
                        f, valid_names);
                    return EXIT_FAILURE;
                }
            }
            break;
        }

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
            if (sscanf(optarg, "%u", &render_options.pad.x) != 1) {
                fprintf(stderr, "%s: invalid padding\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'y':
            if (sscanf(optarg, "%u", &render_options.pad.y) != 1) {
                fprintf(stderr, "%s: invalid padding\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'p':
            if (sscanf(optarg, "%u", &render_options.pad.inner) != 1) {
                fprintf(stderr, "%s: invalid padding\n", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'P': {
            size_t wlen = mbstowcs(NULL, optarg, 0);
            if (wlen == (size_t)-1) {
                fprintf(stderr, "%s: invalid prompt\n", optarg);
                return EXIT_FAILURE;
            }
            prompt_allocated = malloc((wlen + 1) * sizeof(wchar_t));
            if (prompt_allocated == NULL) {
                fprintf(stderr, "%s: invalid prompt\n", optarg);
                return EXIT_FAILURE;
            }
            mbstowcs(prompt_allocated, optarg, wlen + 1);
            prompt_content = prompt_allocated;
            break;
        }

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

        case 'S': {
            uint32_t selection_text_color;
            if (sscanf(optarg, "%x", &selection_text_color) != 1) {
                fprintf(stderr, "%s: invalid color\n", optarg);
                return EXIT_FAILURE;
            }
            render_options.selection_text_color = hex_to_rgba(selection_text_color);
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

        case OPT_LETTER_SPACING: {
            if (!pt_or_px_from_string(optarg, &render_options.letter_spacing))
                return EXIT_FAILURE;
            break;
        }

        case OPT_LAUNCH_PREFIX: {
            launch_prefix = optarg;
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

    log_init(LOG_COLORIZE_AUTO, true, LOG_FACILITY_USER, LOG_CLASS_INFO);
    fcft_log_init(FCFT_LOG_COLORIZE_AUTO, true, FCFT_LOG_CLASS_INFO);

    /* Load applications */
    struct application_list *apps = NULL;
    struct fdm *fdm = NULL;
    struct prompt *prompt = NULL;
    struct matches *matches = NULL;
    struct render *render = NULL;
    struct wayland *wayl = NULL;

    icon_theme_list_t themes = tll_init();

    if (have_another_fuzzel_instance()) {
        LOG_ERR("fuzzel is already running");
        goto out;
    }

    if (icons_enabled) {
        themes = icon_load_theme(icon_theme);
        if (tll_length(themes) > 0)
            LOG_INFO("theme: %s", tll_front(themes).path);
        else
            LOG_WARN("%s: icon theme not found", icon_theme);
    }

    if ((fdm = fdm_init()) == NULL)
        goto out;


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

    if ((prompt = prompt_init(prompt_content)) == NULL)
        goto out;

    if ((matches = matches_init(apps, match_fields)) == NULL)
        goto out;

    struct font_reload_context font_reloaded_data = {
        .icons_enabled = icons_enabled,
        .themes = &themes,
        .apps = apps,
    };

    if ((wayl = wayl_init(
             fdm, render, prompt, matches, &render_options,
             dmenu_mode, launch_prefix, output_name, font_name, dpi_aware,
             &font_reloaded, &font_reloaded_data)) == NULL)
        goto out;

    matches_max_matches_per_page_set(matches, render_options.lines);
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
    free(prompt_allocated);

#if defined(FUZZEL_ENABLE_CAIRO) && defined(_DEBUG)
    cairo_debug_reset_static_data();
#endif
    log_deinit();
    return ret;
}
