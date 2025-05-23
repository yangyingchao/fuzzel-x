fuzzel(1)

# NAME
fuzzel - Wayland app launcher and picker

# SYNOPSIS
*fuzzel* [_OPTIONS_]...

# DESCRIPTION

As a launcher, *fuzzel* lists all available XDG applications in a searchable
window.

With the *--dmenu* flag, fuzzel works like a general-purpose picker like dmenu, rofi or fzf.
Options to choose from are provided on the STDIN and the selected option is printed to STDOUT.

The search box supports Emacs-like key bindings.

Many things can be configured with *fuzzel.ini*(5) or command line options:

*--config*=_PATH_
	Path to configuration file, see *fuzzel.ini*(5) for details.

*--check-config*
	Verify configuration and then exit with 0 if ok, otherwise exit
	with 1.

*--cache*=_PATH_
	Use a custom cache location. Fuzzel uses this file to cache the
	most commonly launched applications (to be able to sort them at
	the top).
	
	You can also use this option to enable caching of dmenu
	entries. It is recommended that you use a separate cache file for
	each "type" of dmenu invocation; i.e. one for the browser history,
	another for emojis etc.

	Set to /dev/null to disable caching.
	
	Default: _XDG_CACHE_HOME/fuzzel_

*-o*,*--output*=_OUTPUT_
	Specifies the monitor to display the window on. Autocompletion is available
	for zsh and fish, or you can list the available outputs with *wlr-randr* or
	with Sway using *swaymsg -t get_outputs*.

	Example: _DP-1_

	Default: Let the compositor choose output.

*-f*,*--font*=_FONT_[,_FALLBACK1_,_FALLBACK2_,...]
	Comma separated list of primary font, and fallback fonts, in
	FontConfig format. See *FONT FORMAT*. Default: _monospace_.

*--use-bold*
	Allow fuzzel to use bold fonts.

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

*-p*,*--prompt*=_PROMPT_
	Prompt to use. Default: _> _.

*--prompt-only*=_PROMPT_
	Same as *--prompt*, but in *--dmenu* mode it also:
	- does not read anything from STDIN
	- sets *--lines* to 0
	
	In non-dmenu mode it behaves exactly like *--prompt*.

*--placeholder*=_TEXT_
	Text to display as placeholder in the input box. Default: empty.

*--search*=_TEXT_
	Initial search/filter string. This option pre-fills the input box
	with the specified string, letting you pre-filter the result.

*-i*
	Ignored; for compatibility with other, similar utilities (where
	*-i* means "case insensitive search").

*--icon-theme*=_NAME_
	Icon theme to use. Note that this option is case sensitive; the
	name must match the theme's folder name.
	
	Example: _Adwaita_.
	
	Default: _hicolor_.

*-I*,*--no-icons*
	Do not render any icons.

*--hide-before-typing*
	Hide application list until something is typed.

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

*-a*,*--anchor*=_ANCHOR_
	Set window anchor, i.e. where on screen the window will be
	displayed.  You can choose one from:
	
	- top-left
	- top
	- top-right
	- left
	- center
	- right
	- bottom-left
	- bottom
	- bottom-right
	
	Default: _center_

*--x-margin*=_MARGIN_
	Horizontal margin away from the anchor point in pixels. Default: _0_.

	Note: this option has no effect when anchor=center, top or bottom.

*--y-margin*=_MARGIN_
	Vertical margin away from the anchor point in pixels. Default: _0_.

	Note: this option has no effect when anchor=center, left or right.

*--select*=_STRING_
	Select the first entry that matches the given string (case
	insensitive).

*--select-index*=INDEX
	Select the entry with index, not compatible with --select.

*-l*,*--lines*=COUNT
	The (maximum) number of matches to display. This dictates the
	window height. Default: _15_.

*-w*,*--width*
	Window width, in characters. Margins and borders not
	included. Default: _30_.

*--tabs*=_COUNT_
	Number of spaces a tab is expanded to. Default: _8_.

*-x*,*--horizontal-pad*=_PAD_
	Horizontal padding between border and icons and text. In pixels,
	subject to output scaling. Default: _40_.

*-y*,*--vertical-pad*=_PAD_
	Vertical padding between border and text. In pixels, subject to
	output scaling. Default: _8_.

*-P*,*--inner-pad*=_PAD_
	Vertical padding between prompt and match list. In pixels, subject to
	output scaling. Default: _0_.

*-b*,*--background*=_HEX_
	Background color. See *COLORS*. Default: _fdf6e3ff_.

*-t*,*--text-color*=_HEX_
	Text color. See *COLORS*. Default: _657b83ff_.

*--prompt-color*=_HEX_
	Text (foreground) color of prompt character(s). See
	*COLORS*. Default: _586e75ff_.

*--placeholder-color*=_HEX_
	Text (foreground) color of the placeholder string. See
	*COLORS*. Default: _93a1a1ff_.

*--input-color*=_HEX_
	Text (foreground) color of input string. See *COLORS*. Default:
	_657b83ff_.

*-m*,*--match-color*=_HEX_
	The color of matching substring(s). As you start typing in the search
	box, the matching part in each application's name is highlighted with
	this color. See *COLORS*. Default: _cb4b16ff_.

*-s*,*--selection-color*=_HEX_
	The color to use as background of the currently selected
	application. See *COLORS*. Default: _eee8d5ff_.

*-S*,*--selection-text-color*=_HEX_
	The text color of the currently selected application. See
	*COLORS*. Default: _586e75ff_.

*-M*,*--selection-match-color*=_HEX_
	The color of matching substring(s) of the currently selected
	application. As you start typing in the search box, the matching
	part in each application's name is highlighted with this color.
	See *COLORS*. Default: _cb4b16ff_.

*--counter-color*=_HEX_
	The color of the match count stats printed at the right-hand side
	of the input prompt. See *COLORS*. Default: _93a1a1ff_.

*-B*,*--border-width*=_INT_
	The width of the surrounding border, in pixels (subject to output
	scaling). Default: _1_.

*-r*,*--border-radius*=_INT_
	The corner curvature, subject to output scaling. Larger means more
	rounded corners. 0 disables rounded corners. Default: _10_.

*-C*,*--border-color*=_HEX_
	The color of the border. See *COLORS*. Default: _002b36ff_.

*--show-actions*
	Include desktop actions in the list. Desktop actions are
	alternative actions some desktop entries have. Examples include
	"New Window", "New Document", etc.

*--match-mode*=_exact_|_fzf_|_fuzzy_
	Defines how what you type is matched. See *fuzzel.ini*(5) for
	details. The default is *fzf*.

*--no-sort*
	Do not sort the result.

*--counter*
	Display the match count.

*--filter-desktop*=[_no_]
	Filter the visible desktop entries based on the value of
	XDG_CURRENT_DESKTOP. If the optional parameter is "no",
	explicitly disables filtering.

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

*--layer*=_top_|_overlay_
	Which layer to render the fuzzel window on. Valid values are *top*
	and *overlay*.
	
	*top* renders above normal windows, but typically below fullscreen
	windows and lock screens.
	
	*overlay* renders on top of both normal windows and fullscreen
	windows. Note that the order is undefined if several windows use
	the same layer. Since e.g. lock screens typically use *overlay*,
	that means fuzzel may or may not appear on top of a lock screen.
	
	Default: _top_

*--keyboard-focus*=_exclusive_|_on-demand_
	Keyboard focusing mode, valid modes are *exclusive* and *on-demand*.
	
	*exclusive* locks keyboard focus to fuzzel, receiving all keyboard
	input until closed.
	
	*on-demand* makes fuzzel focus like a regular window, allowing other
	windows to take keyboard focus by e.g. clicking on them.
	By default switching keyboard focus will immediately close fuzzel,
	using *--no-exit-on-keyboard-focus-loss* allows interacting with
	other windows while fuzzel is open.
	
	Default: _exclusive_

*--no-exit-on-keyboard-focus-loss*
	Do not exit when losing keyboard focus. This can be useful on
	compositors where enabling "focus-follows-mouse" causes fuzzel to
	exit as soon as the mouse is moved over another window. Sway (at
	least up to, and including 1.7) exhibits this behavior.

*--launch-prefix*=_COMMAND_
	Command to launch XDG applications with.
	If set, fuzzel will pass the Desktop File ID of the chosen application
	(see the Desktop Entry specification) in the FUZZEL_DESKTOP_FILE_ID
	environment variables. Default: _not set_.

*--list-executables-in-path*
	Include executables from the PATH environment variable. Applies to
	the normal application mode, not the dmenu mode.

*--render-workers*=_COUNT_
	Number of threads to use for rendering. Set to 0 to disable
	multithreading. Default: the number of available logical CPUs
	(including SMT). Note that this is not always the best value. In
	some cases, the number of physical _cores_ is better.

*--match-workers*=_COUNT_
	Number of threads to use for matching. Set to 0 to disable
	multithreading. Default: the number of available logical CPUs
	(including SMT). Note that this is not always the best value. In
	some cases, the number of physical _cores_ is better.

*--delayed-filter-ms*=_TIME\_MS_
	Time, in milliseconds, to delay refiltering when there are more
	matches than *--delayed-filter-limit*. Default: _300_.

*--delayed-filter-limit*=_N_
	When there are more matches than this, switch from immediate
	refiltering to delayed refiltering (see
	*--delayed-filter-ms*). Default: _20000_.

*-d*,*--dmenu*
	dmenu compatibility mode. In this mode, the list entries are read
	from stdin (newline separated). The selected entry is printed to
	stdout. If the input string does not match any of the entries, the
	input string is printed as is on stdout.
	
	Alternatively, you can symlink the fuzzel binary to
	*dmenu*. Fuzzel will then start in dmenu mode, without the
	*--dmenu* argument.
	
	Fuzzel also supports icons, using Rofi's extended dmenu
	protocol. To set an icon for an entry, append
	*\\0icon\\x1f<icon-name>*. Example:
	
		echo -en "Firefox\\0icon\\x1ffirefox" | fuzzel --dmenu

*--dmenu0*
	Like *--dmenu*, but input is NUL separated instead of newline
	separated. Note that in this mode, icons are not supported.

*--index*
	Print selected entry's index instead of its text.
	Index values start with zero for the first entry.
	dmenu mode only.

*--with-nth*=_N_
	Display the N:th column (tab separated) of each input line. When
	selected, the full input line is printed on stdout. dmenu mode
	only.
	
	Example:
	```
	printf "1\tFirst\n2\tSecond" | fuzzel -d --with-nth=2
	```
	
	This will display the entries *First* and *Second*. When one of
	them is selected, the full input line (tabs included) are printed
	on stdout.
	
	Setting it to *0* is the same as not specifying *--with-nth* at all.

*--accept-nth*=_N_
	Output the N:th column (tab separated) of each input line to stdout.
	dmenu mode only.
	
	Example:
	```
	printf "1\tFirst\n2\tSecond" | fuzzel -d --accept-nth=2
	```
	
	This will display the entries *1   First* and *2   Second*. Depending
	on which one is selected, *First* or *Second* will be printed to stdout.
	
	Setting it to *0* is the same as not specifying *--accept-nth* at all.

*-R*,*--no-run-if-empty*
	Exit immediately, without showing the UI, if stdin is
	empty. dmenu mode only.

*--log-level*={*info*,*warning*,*error*,*none*}
	Log level, used both for log output on stderr as well as
	syslog. Default: _warning_.

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

# LOCALIZATION

Fuzzel uses the localized strings from the .desktop files by
default. To disable this, run fuzzel with *LC_MESSAGES=C*.

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

- *fuzzel.ini*(5)
- *https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html*
- *https://codeberg.org/dnkl/fuzzel*
