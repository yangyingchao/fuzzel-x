fuzzel(1)

# NAME
fuzzel - display XDG applications in a searchable Wayland window

# SYNOPSIS
*fuzzel* [_OPTIONS_]...

# DESCRIPTION

*fuzzel* lists all available XDG applications in a searchable
Window. The search box supports Emacs-like key bindings.

The window size, font and colors can be configured with command line
options:

*-o*,*--output*=_OUTPUT_
	Specifies the monitor to display the window on. In _Sway_, you can
	list the available outputs with *swaymsg -t get_outputs*.

	The default is to let the compositor choose output.

*-f*,*--font*=_FONT_
	Font and style to use, in fontconfig format. See *FONT FORMAT*.
	Default: _monospace_.

*-i*,*--icon-theme*=_NAME_
	Icon theme to use. Example: _Adwaita_. Default: _hicolor_.

*-I*,*--no-icons*
	Do not render any icons.

*-T*,*--terminal*=_TERMINAL ARGS_
	Command to launch XDG applications with the property
	*Terminal=true* (_htop_, for example). Example: _xterm -e_.
	Default: _not set_.

*-l*,*--lines*=COUNT
	The (maximum) number of matches to display. This dictates the
	window height. Default: _15_.

*-w*,*--width*
	Window width, in characters. Margins and borders not
	included. Default: _30_.

*-x*,*--horizontal-pad*=_PAD_
	Horizontal padding between border and icons and text. In pixels,
	subject to output scaling. Default: _40_.

*-y*,*--vertical-pad*=_PAD_
	Vertical padding between border and text. In pixels, subject to
	output scaling. Default: _8_.

*-p*,*--inner-pad*=_PAD_
	Vertical padding between prompt and match list. In pixels, subject to
	output scaling. Default: _0_.

*-b*,*--background*=_HEX_
	Background color. See *COLORS*. Default: _fdf6e3dd_.

*-t*,*--text-color*=_HEX_
	Text color. See *COLORS*. Default: _657b83ff_.

*-m*,*--match-color*=_HEX_
	The color of matching substring(s). As you start typing in the search
	box, the matching part in each application's name is highlighted with
	this color. See *COLORS*. Default: _cb4b16ff_.

*-s*,*--selection-color*=_HEX_
	The color to use as background of the currently selected
	application. See *COLORS*. Default: _eee8d5ff_.

*-B*,*--border-width*=_INT_
	The width of the surrounding border, in pixels (subject to output
	scaling). Default: _1_.

*-r*,*--border-radius*=_INT_
	The corner curvature. Larger means more rounded corners.
	0 disables rounded corners. Default: _10_.

*-C*,*--border-color*=_HEX_
	The color of the border. See *COLORS*. Default: _002b36ff_.

*-d*,*--dmenu*
	dmenu compatibility mode. In this mode, the list entries are read
	from stdin (newline separated). The selected entry is printed to
	stdout. If the input string does not match any of the entries, the
	input string is printed as is on stdout.

*-R*,*--no-run-if-empty*
	Exit immediately, without showing the UI, if stdin is
	empty. **dmenu** mode only.

*--line-height*=_HEIGHT_
	Override line height from font metrics. In points by default, but
	can be specified as pixels by appending 'px'
	(i.e. *--line-height=16px*). Default: _not set_.

*--letter-spacing*=_AMOUNT_
	Additional space between letters. In points by default, but can be
	specified as pixels by appending 'px'
	(i.e. *letter-spacing=5px*). Negative values are
	supported. Default: _0_.

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
without a leading '0x'.

_EXAMPLES_:
- white: *ffffffff* (no transparency)
- black: *000000ff* (no transparency)
- black: *00000010* (semi-transparent)
- red: *ff0000ff* (no transparency)

The default color scheme is _Solarized_.
