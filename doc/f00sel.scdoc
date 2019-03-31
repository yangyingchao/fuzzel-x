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
	list the available outputs with *swaymsg -t get_outputs*.

*-f*,*--font*=_FONT_
	Font to use, in fontconfig format. See *FONT FORMAT*.

*-g*,*--geometry*=_WIDTHxHEIGHT_
	The window width and height

*-b*,*--background*=_HEX_
	Background color. See *COLORS*.

*-t*,*--text-color*=_HEX_
	Text color. See *COLORS*.

*-m*,*--match-color*=_HEX_
	The color of matching substring(s). As you start typing in the search
	box, the matching part in each application's name is highlighted with
	this color. See *COLORS*.

*-s*,*--selection-color*=_HEX_
	The color to use as background of the currently selected
	application. See *COLORS*.

*-B*,*--border-width*=_INT_
	The width of the surrounding border.

*-C*,*--border-color*=_HEX_
	The color of the border. See *COLORS*.

# FONT FORMAT

The font is specified in FontConfig syntax. That is, a colon-separated
list of font name and font options.

_Examples_:
- Dina:weight=bold,slant=italic
- Arial:size=12


# COLORS

All colors must be specified as a RGBA quadruple, in hex format,
without a leading '0x'..

_EXAMPLES_:
- white: *ffffffff* (no transparency)
- black: *000000ff* (no transparency)
- black: *00000010* (semi-transparent)
- red: *ff0000ff* (no transparency)
