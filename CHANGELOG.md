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


### Deprecated
### Removed
### Changed

* PNGs are now loaded and rendered with _libpng_ instead of _cairo_.


### Fixed

* Wrong colors when not fully opaque.


### Security
### Contributors

* [magenbluten](https://codeberg.org/magenbluten)


## 1.4.2

### Fixed

* Subpixel antialiasing was not applied correctly on opaque
  backgrounds.


## 1.4.1

### Fixed

* Incorrect extension for man pages.
