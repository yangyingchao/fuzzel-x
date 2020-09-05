# Changelog

* [Unreleased](#unreleased)
* [1.4.2](#1-4-2)
* [1.4.1](#1-4-1)


## Unreleased
### Added

* meson option `Denable-svg=[auto|enabled|disabled]`. When disabled,
  _librsvg_ is no longer a dependency and SVG icons are
  disabled. Default: `auto`.
* meson option `-Denable-png=[auto|enabled|disabled]`. When disabled,
  _libpng_ is no longer a dependency and PNG icons are
  disabled. Default: `auto`.


### Deprecated
### Removed
### Changed

* PNGs are now loaded and rendered with _libpng_ instead of _cairo_.


### Fixed
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
