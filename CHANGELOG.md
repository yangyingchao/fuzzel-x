# Changelog

* [Unreleased](#unreleased)
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
### Changed
### Deprecated
### Removed
### Fixed
### Security
### Contributors


## 1.7.0

### Added

* `-F,--fields=FIELDS` command line option, allowing you to select
  which XDG Desktop Entry fields to match against
  (https://codeberg.org/dnkl/fuzzel/issues/63).
* Support for desktop entry actions
  (https://codeberg.org/dnkl/fuzzel/issues/71).
* Fuzzy matching. This is enabled by default, but can be disabled with
  `--no-fuzzy`. When enabled, the fuzziness can be adjusted with
  `--fuzzy-max-length-discrepancy` and `--fuzzy-max-distance`
  (https://codeberg.org/dnkl/fuzzel/issues/56).
* `--index` (dmenu mode only): print selected entry’s index instead of
  its text (https://codeberg.org/dnkl/fuzzel/issues/88).
* `--log-level=info|warning|error|none` command line option
  (https://codeberg.org/dnkl/fuzzel/issues/34).
* `--log-no-syslog` command line option.
* `--log-colorize=auto|never|always` command line option.


### Changed

* Fuzzel now refuses to start if there is another fuzzel instance
  running (https://codeberg.org/dnkl/fuzzel/issues/57).
* Treat "Apps" as valid context for applications to support more
  icon themes (for example, Faenza)
* The `Name` entry of the desktop files are no longer used as unique
  identifiers. Instead, we now generate the “desktop file ID”
  according to the XDG desktop entry specification, and use that as ID
  (https://codeberg.org/dnkl/fuzzel/issues/68).
* All XDG data directories are now searched when loading an
  icon. Previously, only XDG data directories where the theme
  directory contained an `index.theme` file were searched
  (https://codeberg.org/dnkl/fuzzel/issues/62).
* Pressing Tab when there is a single match now executes it
  (https://codeberg.org/dnkl/fuzzel/issues/77).
* Use a lock file instead of parsing `/proc` to prevent multiple
  fuzzel instances from running at the same time
  (https://codeberg.org/dnkl/fuzzel/issues/84).
* The application list is now populated in a separate thread, in
  parallel to initializing the GUI. This reduces the risk of missing
  keyboard input (https://codeberg.org/dnkl/fuzzel/issues/82).
* Icons are now loaded in a thread. This allows us to display the
  application list quickly (initially without icons, if loading them
  takes “too” long).
* Fuzzel now exits with exit code 0 when the non-dmenu launcher is
  aborted (no application has been launched) by the user
  (https://codeberg.org/dnkl/fuzzel/issues/98).


### Fixed

* Long entries overrunning the right side padding
  (https://codeberg.org/dnkl/fuzzel/issues/80).
* Tab and Shift+Tab not wrapping around
  (https://codeberg.org/dnkl/fuzzel/issues/78).
* Visual glitches in the corners, when using rounded corners
  (https://codeberg.org/dnkl/fuzzel/issues/90).
* Regression: `--dmenu --lines=0` crashing
  (https://codeberg.org/dnkl/fuzzel/issues/92).


### Contributors

* yangyingchao
* ReplayCoding


## 1.6.5

### Added

* `--dpi-aware=no|yes|auto` command line option.
* Multi-page view (https://codeberg.org/dnkl/fuzzel/issues/42).


### Removed

* Misleading error message about a non-existing cache file
  (https://codeberg.org/dnkl/fuzzel/issues/59).


### Fixed

* Window quickly resized when launched
  (https://codeberg.org/dnkl/fuzzel/issues/21).
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
  KiCAD (https://codeberg.org/dnkl/fuzzel/issues/47).
* SVG icons containing multiple icons not being limited to the main
  icon (https://codeberg.org/dnkl/fuzzel/issues/48).


## 1.6.3

### Added

* `-P,--prompt` command line option, allowing you to set a custom
  prompt.


### Changed

* `-f,--font` now supports explicit font fallbacks.


### Fixed

* Removed usage of deprecated function `rsvg_handle_get_dimensions()`
  when building against recent versions of librsvg
  (https://codeberg.org/dnkl/fuzzel/issues/45).


### Contributors

* [bapt](https://codeberg.org/bapt)


## 1.6.2

### Added

* `-s,--selection-text-color` command line option, that lets you
  configure the foreground/text color of the currently selected item
  (https://codeberg.org/dnkl/fuzzel/issues/37).


### Changed

* Use `rsvg_handle_render_document()` instead of
  `rsvg_handle_render_cairo()` on libsrvg >= 2.46, since the latter
  has been deprecated (https://codeberg.org/dnkl/fuzzel/issues/32).


### Fixed

* Icons not being searched for in all icon theme instances
* Crash when XKB compose file is missing
  (https://codeberg.org/dnkl/fuzzel/issues/35).


## 1.6.1

### Fixed

* Wrong font being used for some entries if guessing monitor fuzzel
  will appear on, and guessing wrong
  (https://codeberg.org/dnkl/fuzzel/issues/31).


## 1.6.0

### Added

* Text shaping support (https://codeberg.org/dnkl/fuzzel/issues/20).
* Option for vertical padding between prompt and match list.


### Changed

* fcft >= 2.4.0 is now required.
* In dmenu mode, fuzzel now prints the keyboard input as is, if it
  does not match any of the items
  (https://codeberg.org/dnkl/fuzzel/issues/23).
* The `.desktop` filename is now also matched against
  (https://codeberg.org/dnkl/fuzzel/issues/25).


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
  (https://codeberg.org/dnkl/fuzzel/issues/17).


## 1.5.1

### Fixed

* Regression: border not being rendered when `--border-radius=0`, or
  if fuzzel was built without cairo
  (https://codeberg.org/dnkl/fuzzel/issues/15).


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
  (https://codeberg.org/dnkl/fuzzel/issues/10).
* `-I,--no-icons` command line option; disables all icons
  (https://codeberg.org/dnkl/fuzzel/issues/12)
* FreeBSD port.
* `-x,--horizontal-pad` and `-y,--vertical-pad` command line options
  (https://codeberg.org/dnkl/fuzzel/issues/12).
* `--line-height` and `-letter-spacing` command line options
  (https://codeberg.org/dnkl/fuzzel/issues/12).


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
