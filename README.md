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
* cairo
* libpng (optional)
* librsvg (optional)
* [fcft](https://codeberg.org/dnkl/fcft) [^1]

[^1]: can also be built as subprojects, in which case they are
    statically linked.


### Building

* meson
* ninja
* wayland protocols
* scdoc
* [tllist](https://codeberg.org/dnkl/tllist) [^1]


## Installation

If you have not installed [tllist](https://codeberg.org/dnkl/tllist)
and [fcft](https://codeberg.org/dnkl/fcft) as system libraries, clone
them into the `subprojects` directory:

```sh
mkdir -p subprojects
pushd subprojects
git clone https://codeberg.org/dnkl/tllist.git
git clone https://codeberg.org/dnkl/fcft.git
popd
```

To build, first, create a build directory, and switch to it:
```sh
mkdir -p bld/release && cd bld/release
```

Second, configure the build (if you intend to install it globally, you
might also want `--prefix=/usr`):
```sh
meson --buildtype=release \
    -Denable-cairo=disabled|enabled|auto \
    -Denable-png=disabled|enabled|auto \
    -Denable-svg=disabled|enabled|auto \
    ../..
```

`-Denable-{png,svg}` can be used to force-enable or force-disable png
and/or svg support. The default is `auto`, which means enable if all
required libraries are available. PNGs require _libpng_, and SVGs
require _cairo_ and _librsvg_.

`-Denable-cairo` can be used to force-enable or force-disable cairo
support. When disabled, fuzzel will not be able to draw rounded
corners, nor will it support SVGs (regardless of what `-Denable-svg`
is set to).

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
