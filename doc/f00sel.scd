f00sel(1)

# NAME
f00sel - display XDG applications in a searchable Wayland window

# SYNOPSIS
*f00sel* [_OPTIONS_]...

# DESCRIPTION

*f00sel* lists all available XDG applications in a searchable
Window. The search box supports Emacs-like key bindings.

The window size, font and colors can be configured with command line
options:

*-o*,*--output*=_OUTPUT_
	Specifies the monitor to display the window on. In _Sway_, you can
	list the available outputs with *swaymsg -t get_outputs*. The default is
	to use the last monitor found.

*-f*,*--font*=_FONT_
	Font and style to use, in fontconfig format. See *FONT FORMAT*.
	Default: _monospace_.

*-i*,*--icon-theme*=_NAME_
	Icon theme name to use. Example: _Adwaita_. Default: _hicolor_.

*-g*,*--geometry*=_WIDTHxHEIGHT_
	The window width and height, in pixels. Default: _500x300_.

*-b*,*--background*=_HEX_
	Background color. See *COLORS*. Default: _000000ff_ (black).

*-t*,*--text-color*=_HEX_
	Text color. See *COLORS*. Default: _ffffffff_ (white).

*-m*,*--match-color*=_HEX_
	The color of matching substring(s). As you start typing in the search
	box, the matching part in each application's name is highlighted with
	this color. See *COLORS*. Default: _cc9393ff_.

*-s*,*--selection-color*=_HEX_
	The color to use as background of the currently selected
	application. See *COLORS*. Default: _333333ff_.

*-B*,*--border-width*=_INT_
	The width of the surrounding border, in pixels. Default: _1_.

*-r*,*--border-radius*=_INT_
	The corner curvature. Larger means more rounded corners.
	0 disables rounded corners. Default: _10_.

*-C*,*--border-color*=_HEX_
	The color of the border. See *COLORS*. Default: _ffffffff_.

*-d*,*--dmenu*
	dmenu compatibility mode. In this mode, the list entries are read
	from stdin (newline separated). The selected entry is printed to
	stdout.

*-v*,*--version*
	Show the version number and quit

# FONT FORMAT

The font is specified in FontConfig syntax. That is, a colon-separated
list of font name and font options.

_Examples_:
- Dina:weight=bold:slant=italic
- Arial:size=12


# COLORS

All colors must be specified as a RGBA quadruple, in hex format,
without a leading '0x'..

_EXAMPLES_:
- white: *ffffffff* (no transparency)
- black: *000000ff* (no transparency)
- black: *00000010* (semi-transparent)
- red: *ff0000ff* (no transparency)
