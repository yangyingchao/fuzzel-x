# Fuzzel

Fuzzel is a Wayland-native application launcher, similar to rofi's
_drun_ mode.


## Screenshot

![Screenshot](doc/screenshot.png)


## Features:

- Wayland native
- Rofi drun-like mode of operation
- dmenu mode where newline separated entries are read from stdin
- Emacs key bindings
- Icons!
- Remembers frequently launched applications


## Limitations:

- No themes (but you **can** configure font and colors)


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
meson --buildtype=release ../..
```

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
