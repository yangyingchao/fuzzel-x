project('f00sel', 'c',
        license: 'MIT',
        default_options: ['c_std=c11', 'warning_level=1', 'werror=true'])

add_project_arguments(
  ['-D_GNU_SOURCE'],
  language: 'c',
)

threads = dependency('threads')
fontconfig = dependency('fontconfig')
cairo = dependency('cairo')
cairo_ft = dependency('cairo-ft')
librsvg = dependency('librsvg-2.0')
wayland_protocols = dependency('wayland-protocols')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
wlroots = dependency('wlroots')
xkb = dependency('xkbcommon')

wscanner = dependency('wayland-scanner', native: true)
wscanner_prog = find_program(
  wscanner.get_pkgconfig_variable('wayland_scanner'), native: true)

wayland_protocol_header = generator(
  wscanner_prog,
  output: '@BASENAME@.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'])
wayland_protocol_source = generator(
  wscanner_prog,
  output: '@BASENAME@.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'])


executable(
  'f00sel',
  'main.c',
  'font.c', 'font.h',
  'icon.c', 'icon.h',
  'render.c', 'render.h',
  'shm.c', 'shm.h',
  'log.c', 'log.h',
  'xdg.c', 'xdg.h',
  'tllist.h',
  wayland_protocol_header.process(
    'external/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml',
    '@0@/stable/xdg-shell/xdg-shell.xml'.format(
      wayland_protocols.get_pkgconfig_variable('pkgdatadir')),
    '@0@/unstable/xdg-output/xdg-output-unstable-v1.xml'.format(
      wayland_protocols.get_pkgconfig_variable('pkgdatadir'))),
  wayland_protocol_source.process(
    'external/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml',
    '@0@/stable/xdg-shell/xdg-shell.xml'.format(
      wayland_protocols.get_pkgconfig_variable('pkgdatadir')),
    '@0@/unstable/xdg-output/xdg-output-unstable-v1.xml'.format(
      wayland_protocols.get_pkgconfig_variable('pkgdatadir'))),
  dependencies: [threads,
                 cairo,
                 cairo_ft,
                 fontconfig,
                 librsvg,
                 wayland_client,
                 wayland_cursor,
                 wlroots,
                 xkb],
  install: true)

subdir('doc')
