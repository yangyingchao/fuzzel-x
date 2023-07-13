# Changelog

* [Unreleased](#unreleased)
* [1.9.1](#1-9-1)
* [1.9.0](#1-9-0)
* [1.8.2](#1-8-2)
* [1.8.1](#1-8-1)
* [1.8.0](#1-8-0)
* [1.7.0](#1-7-0)
* [1.6.5](#1-6-5)
* [1.6.4](#1-6-4)
* [1.6.3](#1-6-3)
* [1.6.2](#1-6-2)
* [1.6.1](#1-6-1)
* [1.6.0](#1-6-0)
* [1.5.4](#1-5-4)
* [1.5.3](#1-5-3)
* [1.5.2](#1-5-2)
* [1.5.1](#1-5-1)
* [1.5.0](#1-5-0)
* [1.4.2](#1-4-2)
* [1.4.1](#1-4-1)


## Unreleased
### Added

* Added a new option `--filter-desktop` which toggles filtering of desktop
  entries based on the OnlyShowIn and NotShowIn keys. Filtering is based on the
  value of $XDG\_CURRENT\_DESKTOP according to desktop-entry spec. Filtering is
  off by default. To disable filtering set in the config from the command line,
  use --filter-desktop=no

### Changed

* Output scaling is now applied to the border radius ([#236][236]).

[236]: https://codeberg.org/dnkl/fuzzel/issues/236


### Deprecated
### Removed
### Fixed

* Last line sometimes not being rendered ([#234][234]).
* `key-bindings.cursor-right-word` not being recognized as a valid
  action.
* `password-character` being set in `fuzzel.ini` incorrectly enabling
  password mode ([#241][241]).
* Added missing zsh+fish completions for `--password`.

[234]: https://codeberg.org/dnkl/fuzzel/issues/234
[241]: https://codeberg.org/dnkl/fuzzel/issues/241


### Security
### Contributors

## 1.9.1

### Fixed

* Regression: default font size was unintentionally changed from 12pt
  in 1.8.2, to 8pt in 1.9.0. The old default of 12pt has now been
  restored.
* Regression: crash when pressing Enter and the match list is empty (e.g. when
  trying to execute a command line) ([#222][222]).

[222]: https://codeberg.org/dnkl/fuzzel/issues/222


## 1.9.0

### Added

* Add support for startup notifications via xdg activation ([#195][195])
* Convert tabs to spaces when rendering ([#137][137]).
* `--dmenu0` command line option. Like `--dmenu`, but input is NUL
  separated instead of newline separated ([#197][197]).
* Support for localized strings. If you want the old behavior, run
  `fuzzel` with `LC_MESSAGES=C` ([#199][199]).
* Export `FUZZEL_DESKTOP_FILE_ID` environment variable when setting
  the `--launch-prefix` in order to pass the Desktop File ID to the
  launch prefix ([#110][110]).
* New key bindings: `[key-bindings].first` and `[key-bindings].last`,
  bound to `Control+Home` and `Control+End` by default ([#210][210]).
* New key binding: `[key-bindings].insert-selected`, bound to
  `Control+Tab` by default. It replaces the current prompt (filter)
  with the selected item ([#212][212]).

[195]: https://codeberg.org/dnkl/fuzzel/pulls/195
[137]: https://codeberg.org/dnkl/fuzzel/issues/137
[197]: https://codeberg.org/dnkl/fuzzel/issues/197
[199]: https://codeberg.org/dnkl/fuzzel/issues/199
[110]: https://codeberg.org/dnkl/fuzzel/issues/110
[210]: https://codeberg.org/dnkl/fuzzel/issues/210
[212]: https://codeberg.org/dnkl/fuzzel/issues/212


### Changed

* Better verification of color values specified on the command line
  ([#194][194]).
* When determining initial font size, do FontConfig config
  substitution if the user-provided font pattern has no {pixel}size
  option ([#1287][foot-1287]).

[194]: https://codeberg.org/dnkl/fuzzel/issues/194
[foot-1287]: https://codeberg.org/dnkl/foot/issues/1287


### Fixed

* Update nanosvg to f0a3e10. Fixes rendering of certain SVG icons
  ([#190][190]).
* Not being able to input numbers using the keypad ([#192][192]).
* Absolute path PNG icons not being loaded ([#214][214]).

[190]: https://codeberg.org/dnkl/fuzzel/issues/190
[192]: https://codeberg.org/dnkl/fuzzel/issues/192
[214]: https://codeberg.org/dnkl/fuzzel/issues/214


### Contributors

* Mark Stosberg
* Max Gautier
* Ronan Pigott


## 1.8.2

### Added

* Fish completions ([#176][176])

[176]: https://codeberg.org/dnkl/fuzzel/issues/176


### Fixed

* Unsupported icon formats not being skipped when loading application
  icons.
* Wrong size of PNG icons selected ([#182][182])

[182]: https://codeberg.org/dnkl/fuzzel/issues/182


## 1.8.1

### Fixed

* Regression: not able to input text with modifiers (e.g. Shift)
  pressed ([#177][177]).

[177]: https://codeberg.org/dnkl/fuzzel/issues/177


## 1.8.0

### Added

* Support for file based configuration ([#3][3]).
* Customizable key bindings ([#117][117]).
* "Custom" key bindings (like Rofi’s `kb-custom-N` key
  bindings).
* If `argv[0]` is _dmenu_, fuzzel now starts in dmenu mode
  ([#107][107]).
* `--password=[CHARACTER]` command line option. Intended to be used
  with “password input”; all typed text is rendered as _CHARACTER_,
  defaulting to `*` if _CHARACTER_ is omitted ([#108][108]).
* `Ctrl+y` binding to execute selected entry.
* `Ctrl+j`/`Ctrl+k` binding to move to the next/previous item
  ([#120][120]).
* Escape sequences in `Exec` arguments are now supported.
* Quoted environment variables in `Exec` arguments are now supported ([#143][143]).
* Multiple space-separated search words can now be entered at the prompt.
* `-M,--selection-match-color`, that lets you configure the color of
  matched substrings of the currently selected item
* New config option `image-size-ratio`, allowing you to control the
  size of the large image displayed when there are only a “few”
  matches.
* Support for icons in dmenu mode, using Rofi’s extended dmenu
  protocol ([#166][166]).
* `--layer` command line option, allowing you to choose which layer to
  render the fuzzel window on (`top` or `overlay`) ([#81][81]).
* `--no-exit-on-keyboard-focus-loss` command line option
  (`exit-on-keyboard-focus-loss` config option) ([#128][128]).

[3]: https://codeberg.org/dnkl/fuzzel/issues/3
[117]: https://codeberg.org/dnkl/fuzzel/issues/117
[107]: https://codeberg.org/dnkl/fuzzel/issues/107
[108]: https://codeberg.org/dnkl/fuzzel/issues/108
[120]: https://codeberg.org/dnkl/fuzzel/issues/120
[143]: https://codeberg.org/dnkl/fuzzel/issues/143
[166]: https://codeberg.org/dnkl/fuzzel/issues/166
[81]: https://codeberg.org/dnkl/fuzzel/issues/81
[128]: https://codeberg.org/dnkl/fuzzel/issues/128


### Changed

* `-i` is now **ignored**. This is to increase compatibility with
  other similar utilities. To set the icon theme, either use the long
  option (`--icon-theme=THEME`), or set it in the configuration file
  (default: `$XDG_CONFIG_HOME/fuzzel/fuzzel.ini`) ([#149][149]).
* Minimum required meson version is now 0.58.
* libpng warnings are now routed through fuzzel’s logging
  ([#101][101]).
* Nanosvg is now the default SVG backend. librsvg is still supported,
  and can be used by setting the `-Dsvg-backend=librsvg` meson option.
* It is no longer necessary to close stdin when using fuzzel in dmenu
  mode, as long as `--no-run-if-empty` is **not** being used
  ([#106][106]).
* Improved performance of initial rendering of icons ([#124][124]).
* `--terminal` now defaults to `$TERMINAL -e`.
* Font shaping is now applied to the prompt
* The large image displayed when there are only a “few” matches is now
  smaller by default.
* Swapped meaning of the command line options `-p` and `-P`; `-p` is
  now the short option for `--prompt` ([#146][146]).
* Do not add icon-sized padding on the left size in dmenu mode
  ([#158][158]).
* Color config values are now allowed to be prefixed with `#`
  ([#160][160]).


[149]: https://codeberg.org/dnkl/fuzzel/issues/149
[101]: https://codeberg.org/dnkl/fuzzel/issues/101
[106]: https://codeberg.org/dnkl/fuzzel/issues/106
[124]: https://codeberg.org/dnkl/fuzzel/issues/124
[146]: https://codeberg.org/dnkl/fuzzel/issues/146
[158]: https://codeberg.org/dnkl/fuzzel/issues/158
[160]: https://codeberg.org/dnkl/fuzzel/issues/160


### Fixed

* User `.desktop` entries with `NoDisplay=true` not overriding system
  entries ([#114][114]).
* Icon lookup is now better at following the XDG specification.
* Backspace removes not only the previous character, but also
  everything **after** the cursor.
* Crash on exit in dmenu mode when selection list is empty.
* Keypad `enter` not executing the selected entry ([#138][138])

[114]: https://codeberg.org/dnkl/fuzzel/issues/114
[138]: https://codeberg.org/dnkl/fuzzel/issues/138


### Contributors

* Chinmay Dalal
* Matthew Toohey
* Michael Yang
* Eyeoglu


## 1.7.0

### Added

* `-F,--fields=FIELDS` command line option, allowing you to select
  which XDG Desktop Entry fields to match against
  ([#63](https://codeberg.org/dnkl/fuzzel/issues/63)).
* Support for desktop entry actions
  ([#71](https://codeberg.org/dnkl/fuzzel/issues/71)).
* Fuzzy matching. This is enabled by default, but can be disabled with
  `--no-fuzzy`. When enabled, the fuzziness can be adjusted with
  `--fuzzy-max-length-discrepancy` and `--fuzzy-max-distance`
  ([#56](https://codeberg.org/dnkl/fuzzel/issues/56)).
* `--index` (dmenu mode only): print selected entry’s index instead of
  its text ([#88](https://codeberg.org/dnkl/fuzzel/issues/88)).
* `--log-level=info|warning|error|none` command line option
  ([#34](https://codeberg.org/dnkl/fuzzel/issues/34)).
* `--log-no-syslog` command line option.
* `--log-colorize=auto|never|always` command line option.


### Changed

* Fuzzel now refuses to start if there is another fuzzel instance
  running ([#57](https://codeberg.org/dnkl/fuzzel/issues/57)).
* Treat "Apps" as valid context for applications to support more
  icon themes (for example, Faenza)
* The `Name` entry of the desktop files are no longer used as unique
  identifiers. Instead, we now generate the “desktop file ID”
  according to the XDG desktop entry specification, and use that as ID
  ([#68](https://codeberg.org/dnkl/fuzzel/issues/68)).
* All XDG data directories are now searched when loading an
  icon. Previously, only XDG data directories where the theme
  directory contained an `index.theme` file were searched
  ([#62](https://codeberg.org/dnkl/fuzzel/issues/62)).
* Pressing Tab when there is a single match now executes it
  ([#77](https://codeberg.org/dnkl/fuzzel/issues/77)).
* Use a lock file instead of parsing `/proc` to prevent multiple
  fuzzel instances from running at the same time
  ([#84](https://codeberg.org/dnkl/fuzzel/issues/84)).
* The application list is now populated in a separate thread, in
  parallel to initializing the GUI. This reduces the risk of missing
  keyboard input ([#82](https://codeberg.org/dnkl/fuzzel/issues/82)).
* Icons are now loaded in a thread. This allows us to display the
  application list quickly (initially without icons, if loading them
  takes “too” long).
* Fuzzel now exits with exit code 0 when the non-dmenu launcher is
  aborted (no application has been launched) by the user
  ([#98](https://codeberg.org/dnkl/fuzzel/issues/98)).


### Fixed

* Long entries overrunning the right side padding
  ([#80](https://codeberg.org/dnkl/fuzzel/issues/80)).
* Tab and Shift+Tab not wrapping around
  ([#78](https://codeberg.org/dnkl/fuzzel/issues/78)).
* Visual glitches in the corners, when using rounded corners
  ([#90](https://codeberg.org/dnkl/fuzzel/issues/90)).
* Regression: `--dmenu --lines=0` crashing
  ([#92](https://codeberg.org/dnkl/fuzzel/issues/92)).


### Contributors

* yangyingchao
* ReplayCoding


## 1.6.5

### Added

* `--dpi-aware=no|yes|auto` command line option.
* Multi-page view ([#42](https://codeberg.org/dnkl/fuzzel/issues/42)).


### Removed

* Misleading error message about a non-existing cache file
  ([#59](https://codeberg.org/dnkl/fuzzel/issues/59)).


### Fixed

* Window quickly resized when launched
  ([#21](https://codeberg.org/dnkl/fuzzel/issues/21)).
* Layer surface being committed before configure event has been ack:ed.


## 1.6.4

### Added

* Support for [nanosvg](https://github.com/memononen/nanosvg) as an
  alternative SVG backend. Nanosvg is bundled with fuzzel and has
  **no** additional dependencies. This means you can now have SVGs
  without depending on Cairo.


### Changed

* Meson option `-Denable-png` replaced with `-Dpng-backend=none|libpng`.
* Meson option `-Denable-svg` replaced with `-Dsvg-backend=none|librsvg|nanosvg
* fcft >= 3.0 is now required.
* `-f,--font` now supports explicit font fallbacks.


### Fixed

* Graphical corruption triggered by the “gerbview” SVG icon, from
  KiCAD ([#47](https://codeberg.org/dnkl/fuzzel/issues/47)).
* SVG icons containing multiple icons not being limited to the main
  icon ([#48](https://codeberg.org/dnkl/fuzzel/issues/48)).


## 1.6.3

### Added

* `-P,--prompt` command line option, allowing you to set a custom
  prompt.


### Changed

* `-f,--font` now supports explicit font fallbacks.


### Fixed

* Removed usage of deprecated function `rsvg_handle_get_dimensions()`
  when building against recent versions of librsvg
  ([#45](https://codeberg.org/dnkl/fuzzel/issues/45)).


### Contributors

* [bapt](https://codeberg.org/bapt)


## 1.6.2

### Added

* `-s,--selection-text-color` command line option, that lets you
  configure the foreground/text color of the currently selected item
  ([#37](https://codeberg.org/dnkl/fuzzel/issues/37)).


### Changed

* Use `rsvg_handle_render_document()` instead of
  `rsvg_handle_render_cairo()` on libsrvg >= 2.46, since the latter
  has been deprecated ([#32](https://codeberg.org/dnkl/fuzzel/issues/32)).


### Fixed

* Icons not being searched for in all icon theme instances
* Crash when XKB compose file is missing
  ([#35](https://codeberg.org/dnkl/fuzzel/issues/35)).


## 1.6.1

### Fixed

* Wrong font being used for some entries if guessing monitor fuzzel
  will appear on, and guessing wrong
  ([#31](https://codeberg.org/dnkl/fuzzel/issues/31)).


## 1.6.0

### Added

* Text shaping support ([#20](https://codeberg.org/dnkl/fuzzel/issues/20)).
* Option for vertical padding between prompt and match list.


### Changed

* fcft >= 2.4.0 is now required.
* In dmenu mode, fuzzel now prints the keyboard input as is, if it
  does not match any of the items
  ([#23](https://codeberg.org/dnkl/fuzzel/issues/23)).
* The `.desktop` filename is now also matched against
  ([#25](https://codeberg.org/dnkl/fuzzel/issues/25)).


### Fixed

* Set initial subpixel mode correctly when there is only one monitor.
* Crash when `~/.cache/fuzzel` contained invalid/corrupt entries.


### Contributors

* [loserMcloser](https://codeberg.org/loserMcloser)


## 1.5.4

### Fixed

* Icon size calculation with scaling factors > 1


## 1.5.3

### Fixed

* Compilation when both PNGs and SVGs have been disabled.


## 1.5.2

### Changed

* Maximum icon height reduced, from the `line height`, to the `line
  height` minus the font's `descent`. This ensures a margin between
  icons.


### Fixed

* Crash when compositor provided bad monitor geometry data
  ([#17](https://codeberg.org/dnkl/fuzzel/issues/17)).


## 1.5.1

### Fixed

* Regression: border not being rendered when `--border-radius=0`, or
  if fuzzel was built without cairo
  ([#15](https://codeberg.org/dnkl/fuzzel/issues/15)).


## 1.5.0

### Added

* meson option `-Denable-svg=[auto|enabled|disabled]`. When disabled,
  _librsvg_ is no longer a dependency and SVG icons are
  disabled. Default: `auto`.
* meson option `-Denable-png=[auto|enabled|disabled]`. When disabled,
  _libpng_ is no longer a dependency and PNG icons are
  disabled. Default: `auto`.
* meson option `-Denable-cairo=[auto|enabled|disabled]`. When
  disabled, fuzzel will not be able to draw rounded corners, nor
  support SVGs (regardless of what `-Denable-svg` is set to)
  ([#10](https://codeberg.org/dnkl/fuzzel/issues/10)).
* `-I,--no-icons` command line option; disables all icons
  ([#12](https://codeberg.org/dnkl/fuzzel/issues/12))
* FreeBSD port.
* `-x,--horizontal-pad` and `-y,--vertical-pad` command line options
  ([#12](https://codeberg.org/dnkl/fuzzel/issues/12)).
* `--line-height` and `-letter-spacing` command line options
  ([#12](https://codeberg.org/dnkl/fuzzel/issues/12)).


### Changed

* PNGs are now loaded and rendered with _libpng_ instead of _cairo_.


### Fixed

* Wrong colors when not fully opaque.
* Rendering of SVGs containing multiple icons.
* Font being incorrectly scaled on rotated monitors.
* PPI being calculated incorrectly.
* Crash on keyboard input when repeat rate was zero (i.e. no repeat).


### Contributors

* [magenbluten](https://codeberg.org/magenbluten)
* jbeich


## 1.4.2

### Fixed

* Subpixel antialiasing was not applied correctly on opaque
  backgrounds.


## 1.4.1

### Fixed

* Incorrect extension for man pages.
