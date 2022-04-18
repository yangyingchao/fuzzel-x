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


# SEE ALSO

*fuzzel*(1)
