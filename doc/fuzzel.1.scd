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

*--config*=_PATH_
	Path to configuration file, see *fuzzel.ini*(5) for details.

*-o*,*--output*=_OUTPUT_
	Specifies the monitor to display the window on. In _Sway_, you can
	list the available outputs with *swaymsg -t get_outputs*.

	The default is to let the compositor choose output.

*-f*,*--font*=_FONT_[,_FALLBACK1_,_FALLBACK2_,...]
	Comma separated list of primary font, and fallback fonts, in
	FontConfig format. See *FONT FORMAT*. Default: _monospace_.

*-D*,*--dpi-aware*=_no|yes|auto_
	When set to *yes*, fonts are sized using the monitor's DPI, making
	a font of a given point size have the same physical size,
	regardless of monitor.
	
	In this mode, the monitor's scaling factor is ignored; doubling
	the scaling factor will *not* double the font size.
	
	When set to *no*, the monitor's DPI is ignored. The font is
	instead sized using the monitor's scaling factor; doubling the
	scaling factor *does* double the font size.
	
	Finally, if set to *auto*, fonts will be sized using the monitor's
	DPI if _all_ monitors have a scaling factor of 1. If at least one
	monitor as a scaling factor larger than 1 (regardless of whether
	the fuzzel window is mapped on that monitor or not), fonts will be
	scaled using the scaling factor.
	
	Note that this option typically does not work with bitmap fonts,
	which only contains a pre-defined set of sizes, and cannot be
	dynamically scaled. Whichever size (of the available ones) that
	best matches the DPI or scaling factor, will be used.
	
	Also note that if the font size has been specified in pixels
	(*:pixelsize=*_N_, instead of *:size=*_N_), DPI scaling
	(*dpi-aware=yes*) will have no effect (the specified pixel size
	will be used as is). But, if the monitor's scaling factor is used
	to size the font (*dpi-aware=no*), the font's pixel size will be
	multiplied with the scaling factor.
	
	Default: _auto_

*-P*,*--prompt*=_PROMPT_
	Prompt to use. Default: _> _.

*-i*,*--icon-theme*=_NAME_
	Icon theme to use. Note that this option is case sensitive; the
	name must match the theme's folder name.
	
	Example: _Adwaita_.
	
	Default: _hicolor_.

*-I*,*--no-icons*
	Do not render any icons.

*-F*,*--fields*=_FIELDS_
	Comma separated list of XDG Desktop entry fields to match against:
	
	- *filename*: the .desktop file's filename
	- *name*: the application's name (title)
	- *generic*: the application's generic name
	- *exec*: the applications's executable, as specified in the desktop file. Note: may include command line options as well.
	- *keywords*: the application's keywords
	- *categories*: the application's categories
	- *comment*: the application's comment
	
	Default: _filename,name,generic_

*--password*=[_CHARACTER_]
	Password input. Render all typed text as _CHARACTER_. If
	_CHARACTER_ is omitted, a *\** will be used.

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
	application. See *COLORS*. Default: _eee8d5dd_.

*-S*,*--selection-text-color*=_HEX_
	The text color of the currently selected application. See
	*COLORS*. Default: _657b83ff_.

*-B*,*--border-width*=_INT_
	The width of the surrounding border, in pixels (subject to output
	scaling). Default: _1_.

*-r*,*--border-radius*=_INT_
	The corner curvature. Larger means more rounded corners.
	0 disables rounded corners. Default: _10_.

*-C*,*--border-color*=_HEX_
	The color of the border. See *COLORS*. Default: _002b36ff_.

*--show-actions*
	Include desktop actions in the list. Desktop actions are
	alternative actions some desktop entries have. Examples include
	"New Window", "New Document", etc.

*--no-fuzzy*
	Disables fuzzy matching. When disabled, only exact (case
	insensitive) substring matches are considered.

*--fuzzy-min-length*=_VALUE_
	Search strings shorter than this will not by fuzzy
	matched. Default: _3_.

*--fuzzy-max-length-discrepancy*=_VALUE_
	Maximum allowed length difference between the search string, and a
	fuzzy match. Larger values result in more fuzzy matches. Default:
	_2_.

*--fuzzy-max-distance*=_VALUE_
	Maximum allowed levenshtein distance between the search string,
	and a fuzzy match. Larger values result in more fuzzy
	matches. Default: _1_.

*--line-height*=_HEIGHT_
	Override line height from font metrics. In points by default, but
	can be specified as pixels by appending 'px'
	(i.e. *--line-height=16px*). Default: _not set_.

*--letter-spacing*=_AMOUNT_
	Additional space between letters. In points by default, but can be
	specified as pixels by appending 'px'
	(i.e. *letter-spacing=5px*). Negative values are
	supported. Default: _0_.

*--launch-prefix*=_COMMAND_
	Command to launch XDG applications with. Example: _swaymsg exec --_.
	Default: _not set_.

*-d*,*--dmenu*
	dmenu compatibility mode. In this mode, the list entries are read
	from stdin (newline separated). The selected entry is printed to
	stdout. If the input string does not match any of the entries, the
	input string is printed as is on stdout.
	
	Alternatively, you can symlink the fuzzel binary to
	*dmenu*. Fuzzel will then start in dmenu mode, without the
	*--dmenu* argument.

*--index*
	Print selected entry's index instead of its text. **dmenu** mode
	only.

*-R*,*--no-run-if-empty*
	Exit immediately, without showing the UI, if stdin is
	empty. **dmenu** mode only.

*--log-level*={*info*,*warning*,*error*,*none*}
	Log level, used both for log output on stderr as well as
	syslog. Default: _info_.

*--log-colorize*=[{*never*,*always*,*auto*}]
	Enables or disables colorization of log output on stderr.

*--log-no-syslog*
	Disables syslog logging. Logging is only done on stderr.

*-v*,*--version*
	Show the version number and quit

# CONFIGURATION

fuzzel will search for a configuration file in the following locations,
in this order:

	- *XDG_CONFIG_HOME/fuzzel/fuzzel.ini* (defaulting to
	  *~/.config/fuzzel/fuzzel.ini* if unset)
	- *XDG_CONFIG_DIRS/fuzzel/fuzzel.ini* (defaulting to
	  */etc/xdg/fuzzel/fuzzel.ini* if unset)

An example configuration file containing all options with their default value
commented out will usually be installed to */etc/xdg/fuzzel/fuzzel.ini*.

For more information, see *fuzzel.ini*(5).

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

# FILES

_$XDG_CACHE_HOME/fuzzel_
	Stores a list of applications and their launch count. This allows
	fuzzel to sort frequently launched applications at the top.

_$XDG_RUNTIME_DIR/fuzzel-$WAYLAND_DISPLAY.lock_
	Lock file, used to prevent multiple fuzzel instances from running
	at the same time.

# SEE ALSO

*fuzzel.ini*(5)
