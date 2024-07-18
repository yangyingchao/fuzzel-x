[![CI status](https://ci.codeberg.org/api/badges/dnkl/fuzzel/status.svg)](https://ci.codeberg.org/dnkl/fuzzel)

# Fuzzel

Fuzzel is a Wayland-native application launcher, similar to rofi's
_drun_ mode.

[![Packaging status](https://repology.org/badge/vertical-allrepos/fuzzel.svg)](https://repology.org/project/fuzzel/versions)


## Screenshot

![Screenshot](doc/screenshot.png)

_Fuzzel, with transparency, on top of a browser window showing a diff of a fuzzel commit_


## Features:

- Wayland native
- Rofi drun-like mode of operation
- dmenu mode where newline separated entries are read from stdin
- Emacs key bindings
- Icons!
- Remembers frequently launched applications


## Limitations:

- No themes (but you **can** configure font and colors)


## Requirements

### Runtime

* pixman
* wayland (_client_ and _cursor_ libraries)
* xkbcommon
* cairo (optional, required by librsvg)
* libpng (optional)
* librsvg (optional, for enhanced SVG icon support)
* [fcft](https://codeberg.org/dnkl/fcft) [^1]

Fuzzel uses the builtin nanosvg backend to render SVG icons by
default. Since nanosvg is somewhat limited, we also offer a librsvg
backend for SVG icons. Note that librsvg also requires cairo.

[^1]: can also be built as subprojects, in which case they are
    statically linked.


### Building

* meson
* ninja
* wayland protocols
* scdoc
* [tllist](https://codeberg.org/dnkl/tllist) [^1]


## Installation

To build, first, create a build directory, and switch to it:
```sh
mkdir -p bld/release && cd bld/release
```

Second, configure the build (if you intend to install it globally, you
might also want `--prefix=/usr`):
```sh
meson --buildtype=release \
    -Denable-cairo=disabled|enabled|auto \
    -Dpng-backend=none|libpng \
    -Dsvg-backend=none|librsvg|nanosvg \
    ../..
```

`-D{png,svg}-backend` can be used to force-enable or force-disable a
specific png and/or svg backend. Note that _nanosvg_ is builtin
(i.e. it needs to external dependencies).

`-Denable-cairo` can be used to force-enable or force-disable cairo
support. When disabled, fuzzel will not be able to draw rounded
corners, nor will it support SVGs using the _librsvg_ backend.

Three, build it:
```sh
ninja
```

You can now run it directly from the build directory:
```sh
./fuzzel
```

Use command line arguments to configure the look-and-feel:
```sh
./fuzzel --help
```

Optionally, install it:
```sh
ninja install
```

For more detailed configuration information, see the man page:
```sh
man fuzzel
```


## License

Fuzzel is released under the [MIT license](LICENSE).

Fuzzel uses nanosvg, released under the [Zlib
license](3rd-party/nanosvg/LICENSE.txt).
