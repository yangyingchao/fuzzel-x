# Changelog

* [Unreleased](#unreleased)
* [1.4.2](#1-4-2)
* [1.4.1](#1-4-1)


## Unreleased
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


### Deprecated
### Removed
### Changed

* PNGs are now loaded and rendered with _libpng_ instead of _cairo_.


### Fixed

* Wrong colors when not fully opaque.
* Rendering of SVGs containing multiple icons.
* Font being incorrectly scaled on rotated monitors.
* PPI being calculated incorrectly.
* Crash on keyboard input when repeat rate was zero (i.e. no repeat).


### Security
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
