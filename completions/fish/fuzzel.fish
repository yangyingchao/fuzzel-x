function __fish_complete_fuzzel_output
  if type -q wlr-randr;
    wlr-randr | grep -e '^[^[:space:]]\+' | cut -d ' ' -f 1
  else if type -q swaymsg;
    swaymsg -t get_outputs --raw|grep name|cut -d '"' -f 4
  end
end

complete -c fuzzel

complete -c fuzzel -f
complete -c fuzzel -r -s c -l config                                                                            -d "path to configuration file (XDG_CONFIG_HOME/fuzzel/fuzzel.ini)"
complete -c fuzzel -r      -l cache                                                                             -d "file to load most recently launched applications from (XDG_CACHE_HOME/fuzzel)"
complete -c fuzzel         -l check-config                                                                      -d "verify configuration and exit with 0 if ok, otherwise exit with 1"
complete -c fuzzel -x -s f -l font               -a "(fc-list : family | sed 's/,/\n/g' | sort | uniq)"         -d "font name and style in fontconfig format (monospace)"
complete -c fuzzel         -l use-bold                                                                          -d "allow fuzzel to use bold fonts"
complete -c fuzzel -x -s o -l output             -a "(__fish_complete_fuzzel_output)" -d "output (monitor) do display on (none)"
complete -c fuzzel -x -s D -l dpi-aware          -a "no yes auto"                                               -d "scale fonts using the monitor's DPI (auto)"
complete -c fuzzel -x      -l icon-theme         -a "(find /usr/share/icons -mindepth 1 -maxdepth 1 -type d -print0 | xargs -0 -n 1 basename | sort)" -d "icon theme name (hicolor)"
complete -c fuzzel    -s I -l no-icons                                                                          -d "do not render any icons"
complete -c fuzzel         -l hide-before-typing                                                            -d "hide application list until something is typed"
complete -c fuzzel -x -s F -l fields             -a "filename name generic exec categories keywords comment"    -d "comma separated list of XDG Desktop entry fields to match"
complete -c fuzzel -x -s p -l prompt                                                                            -d "string to use as input prompt (\"> \")"
complete -c fuzzel -x      -l placeholder                                                                       -d "placeholder text in input box"
complete -c fuzzel -x      -l search                                                                            -d "initial search/filter string"
complete -c fuzzel         -l password           -a ""                                                          -d "render all input using either '*', or the specified character"

# TODO: this currently doesn’t quote the completed argument
complete -c fuzzel -x -s T -l terminal           -a "(__fish_complete_subcommand)"                              -d "terminal command, with arguments ($TERMINAL -e)"

complete -c fuzzel -x -s a -l anchor             -a "top-left top top-right left center right bottom-left bottom bottom-right" -d "set window anchor (center)"
complete -c fuzzel -x      -l x-margin                                                                          -d "horizontal margin away from the anchor point in pixels (0)"
complete -c fuzzel -x      -l y-margin                                                                          -d "vertical margin away from the anchor point in pixels (0)"
complete -c fuzzel -x      -l select                                                                            -d "select the first entry that matches the given string"
complete -c fuzzel -x      -l select-index                                                                      -d "select the entry at index"
complete -c fuzzel -x -s l -l lines                                                                             -d "maximum number of matches to displayh (15)"
complete -c fuzzel -x -s w -l width                                                                             -d "window width, in characters (30)"
complete -c fuzzel -x -s x -l horizontal-pad                                                                    -d "horizontal padding, in pixels (40)"
complete -c fuzzel -x -s y -l vertical-pad                                                                      -d "vertical padding, in pixels (8)"
complete -c fuzzel -x -s P -l inner-pad                                                                         -d "vertical padding between prompt and matches, in pixels (0)"
complete -c fuzzel -x -s b -l background                                                                        -d "background color (fdf6e3ff)"
complete -c fuzzel -x -s t -l text-color                                                                        -d "text color (657b83ff)"
complete -c fuzzel -x      -l prompt-color                                                                      -d "color of the prompt text (586e75ff)"
complete -c fuzzel -x      -l placeholder-color                                                                 -d "color of the placeholder text (93a1a1ff)"
complete -c fuzzel -x      -l input-color                                                                       -d "color of the input string (657b83ff)"
complete -c fuzzel -x -s m -l match-color                                                                       -d "color of matched substring (cb4b16ff)"
complete -c fuzzel -x -s s -l selection-color                                                                   -d "background color of selected item (eee8d5ff)"
complete -c fuzzel -x -s S -l selection-text-color                                                              -d "text color of selected item (586e75ff)"
complete -c fuzzel -x -s M -l selection-match-color                                                             -d "color of matched substring of selected item (cb4b16ff)"
complete -c fuzzel -x      -l counter-color                                                                     -d "color of the match count (93a1a1ff)"
complete -c fuzzel -x -s B -l border-width                                                                      -d "width of border, in pixels (1)"
complete -c fuzzel -x -s r -l border-radius                                                                     -d "amount of corner \"roundness\" (10)"
complete -c fuzzel -x -s C -l border-color                                                                      -d "border color (002b36ff)"
complete -c fuzzel         -l show-actions                                                                      -d "include desktop actions (e.g. \"New Window\") in the list"
complete -c fuzzel -x      -l match-mode         -a "exact fzf fuzzy"                                           -d "how to match what you type against the entries"
complete -c fuzzel         -l no-sort                                                                           -d "do not sort the result"
complete -c fuzzel         -l counter                                                                           -d "display the match count"
complete -c fuzzel         -l filter-desktop                                                                    -d "filter desktop entries based on XDG_CURRENT_DESKTOP"
complete -c fuzzel         -l list-executables-in-path                                                          -d "list executables present in the path environment variable"
complete -c fuzzel -x      -l fuzzy-min-length                                                                  -d "search strings shorter than this will not be fuzzy matched (3)"
complete -c fuzzel -x      -l fuzzy-max-length-discrepancy                                                      -d "maximum allowed length difference between the search string and a fuzzy match (2)"
complete -c fuzzel -x      -l fuzzy-max-distance                                                                -d "maximum allowed levenshtein distance between the search string and a fuzzy match (1)"
complete -c fuzzel -x      -l line-height                                                                       -d "override the line height from font metrics, in points or pixels"
complete -c fuzzel -x      -l letter-spacing                                                                    -d "additional letter spacing, in points or pixels"
complete -c fuzzel -x      -l layer              -a "top overlay"                                               -d "which layer to render the fuzzel window on (top)"
complete -c fuzzel -x      -l keyboard-focus     -a "exclusive on-demand"                                       -d "keyboard focus mode (exclusive)"
complete -c fuzzel -x      -l render-workers                                                                    -d "number of render worker threads"
complete -c fuzzel -x      -l match-workers                                                                     -d "number of match worker threads"
complete -c fuzzel -x      -l delayed-filter-ms                                                                 -d "time in milliseconds to delay refiltering when there are lots of matches"
complete -c fuzzel -x      -l delayed-filter-limit                                                              -d "switch to delayed refiltering when there are more matches than this"
complete -c fuzzel    -s d -l dmenu                                                                             -d "dmenu compatibility mode; entries are read from stdin, newline separated"
complete -c fuzzel         -l dmenu0                                                                            -d "dmenu compatibility mode; entries are read from stdin, NUL separated"
complete -c fuzzel         -l index                                                                             -d "print selected entry's index instead of its text (dmenu mode only)"
complete -c fuzzel -x      -l with-nth                                                                          -d "display the N:th column (tab separated) of each input line (dmenu only)"
complete -c fuzzel -x      -l accept-nth                                                                        -d "output the N:th column (tab separated) of each input line (dmenu only)"
complete -c fuzzel    -s R -l no-run-if-empty                                                                   -d "exit immediately without showing the UI if stdin is empty (dmenu mode only)"
complete -c fuzzel -x -s d -l log-level          -a "info warning error none"                                   -d "log-level (warning)"
complete -c fuzzel -x -s l -l log-colorize       -a "always never auto"                                         -d "enable or disable colorization of log output on stderr"
complete -c fuzzel    -s S -l log-no-syslog                                                                     -d "disable syslog logging"
complete -c fuzzel    -s v -l version                                                                           -d "show the version number and quit"
complete -c fuzzel    -s h -l help                                                                              -d "show help message and quit"
