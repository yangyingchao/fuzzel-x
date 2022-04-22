fuzzel.ini(5)

# NAME
fuzzel.ini - configuration file for *fuzzel*(1)


# DESCRIPTION

*fuzzel* uses the standard _unix configuration format_, with section
based key/value pairs. The default section is usually unnamed,
i.e. not prefixed with a _[section]_. However it can also be
explicitly named _[main]_, say if it needs to be reopened after any of
the other sections.

fuzzel will search for a configuration file in the following locations,
in this order:

	- *XDG_CONFIG_HOME/fuzzel/fuzzel.ini* (defaulting to
	  *~/.config/fuzzel/fuzzel.ini* if unset)
	- *XDG_CONFIG_DIRS/fuzzel/fuzzel.ini* (defaulting to
	  */etc/xdg/fuzzel/fuzzel.ini* if unset)

An example configuration file containing all options with their default value
commented out will usually be installed to */etc/xdg/fuzzel/fuzzel.ini*.

# SECTION: main

*output*
	Output (monitor) to display on. If unset, the compositor will
	choose the monitor. Default: _not set_.

*font*
	Comma separated list of fonts to use, in fontconfig format. That
	is, a font name followed by a list of colon-separated
	options. Most noteworthy is *:size=n*, which is used to set the
	font size. Note that the font size is also affected by the
	*dpi-aware* option.
	
	Examples:
		- Dina:weight=bold:slant=italic
		- Courier New:size=12
		- Fantasque Sans Mono:fontfeatures=ss01
	
	Default: _monospace_.

*dpi-aware*
	*auto*, *yes*, or *no*.
	
	When set to *yes*, fonts are sized using the monitor's DPI, making
	a font of a given size have the same physical size, regardless of
	monitor.
	
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
	
	Default: _auto_.

*prompt*=
	String to use as input prompt. Default: _> _.

*icon-theme*
	Icon theme. Note that the name is case sensitive. Default:
	_hicolor_.

*icons-enabled*
	Boolean. When enabled, application icons (from the selected
	*icon-theme*) will be rendered. Default: _yes_.

*fields*
	Comma separated list of XDG Desktop entry fields to match
	against.
	
	Even though fuzzel only displays the application names,
	matching can (and by default is) be done against other fields as
	well. Supported fields are:
	
	- filename
	- name
	- generic
	- exec
	- categories
	- keywords
	- comment
	
	Default: _filename,name,generic_.

*password-character*
	Default character to use with the command line option
	*--password*. Note that this can be overridden on the command
	line, by explicitly specifying the character with
	*--password=X*. Default: _\*_.

*fuzzy*
	Boolean. Enables or disables fuzzy matching. Default: _yes_.

*show-actions*
	Boolean. Some desktop files define "actions", in addition to the
	application itself. Examples are "new window", "preferences",
	etc. When this option is enabled, those actions will be listed by
	fuzzel. Default: _no_.

*terminal*
	terminal command to use when launching 'terminal' programs,
	e.g. \"xterm -e\". Default: _not set_.

*launch-prefix*
	Prefix to add before argv of executed program. Default: _not set_.

*lines*
	Number of matches to show. Default: _15_.

*width*
	Window width, in characters. Border and margins are not included
	in this. Default: _30_.

*horizontal-pad*
	Horizontal padding, in pixels, between border and
	content. Default: _40_.

*vertical-pad*
	Vertical padding, in pixels, between border and content. Default:
	_8_.

*inner-pad*
	Vertical padding between prompt and match list, in
	pixels. Default: _0_.

*line-height*
	Override line height from font metrics. Default: use font metrics.

*letter-spacing*
	Additional letter spacing. Negative values are allowed. Default:
	_0_.


# SECTION: colors

All color values are in RGBA.

*background*
	Background color. Default: _fdf6e3dd_.

*text*
	Text (foreground) color of unselected entries. Default:
	_657b83ff_.

*match*
	Text (foreground) color of the matched substring. Default:
	_cb4b16ff_.

*selection*
	Background color of the selected entry. Default: _eee8d5dd_.

*selection-text*
	Text (foreground) color of the selected entry. Default:
	_657b83ff_.

*border*
	Border color. Default: _002b36ff_.


# SECTION: border

*width*
	Width of the border, in pixels. Default: _1_.

*radius*
	Amount of corner "roundness". Default: _10_.


# SECTION: dmenu

*mode*
	One of *text* or *index*. Determines what fuzzel prints on stdout
	when an entry is selected: *text* prints the entry itself, *index*
	prints the index of the selected entry. Default: _text_.

*exit-immediately-if-empty*
	Boolean. If enabled, fuzzel will not run at all (i.e. it will not
	open a window, and will not print anything on stdout) if there's
	nothing on stdin. Default: _no_.


# SEE ALSO

*fuzzel*(1)
