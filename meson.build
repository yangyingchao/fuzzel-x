project('fuzzel', 'c',
        version: '1.6.1',
        license: 'MIT',
        meson_version: '>=0.53.0',
        default_options: [
          'c_std=c18',
          'warning_level=1',
          'werror=true',
          'b_ndebug=if-release'])

is_debug_build = get_option('buildtype').startswith('debug')

add_project_arguments(
  ['-D_GNU_SOURCE'] +
  (is_debug_build ? ['-D_DEBUG'] : []),
  language: 'c',
)

cc = meson.get_compiler('c')

if cc.has_function('memfd_create')
  add_project_arguments('-DMEMFD_CREATE', language: 'c')
endif

# Compute the relative path used by compiler invocations.
source_root = meson.current_source_dir().split('/')
build_root = meson.build_root().split('/')
relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
  if i >= source_root.length() or not in_prefix or p != source_root[i]
    in_prefix = false
    relative_dir_parts += '..'
  endif
  i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
  if i >= build_root.length() or not in_prefix or build_root[i] != p
    in_prefix = false
    relative_dir_parts += p
  endif
  i += 1
endforeach
relative_dir = join_paths(relative_dir_parts) + '/'

if cc.has_argument('-fmacro-prefix-map=/foo=')
  add_project_arguments('-fmacro-prefix-map=@0@='.format(relative_dir), language: 'c')
endif

math = cc.find_library('m')
pixman = dependency('pixman-1')
cairo = dependency('cairo', required: get_option('enable-cairo'))
libepoll = dependency('epoll-shim', required: false)
libpng = dependency('libpng', required: get_option('enable-png'))
if cairo.found()
  librsvg = dependency('librsvg-2.0', required: get_option('enable-svg'))
else
  librsvg = declare_dependency()
endif
wayland_protocols = dependency('wayland-protocols')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
xkb = dependency('xkbcommon')

if cairo.found()
  add_project_arguments('-DFUZZEL_ENABLE_CAIRO=1', language: 'c')
endif

if libpng.found()
  add_project_arguments('-DFUZZEL_ENABLE_PNG=1', language: 'c')
endif

if librsvg.found() and cairo.found()
  add_project_arguments('-DFUZZEL_ENABLE_SVG=1', language: 'c')
endif

tllist = dependency('tllist', version: '>=1.0.1', fallback: 'tllist')
fcft = dependency('fcft', version: ['>=2.4.0', '<3.0.0'], fallback: 'fcft')

wayland_protocols_datadir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')

wscanner = dependency('wayland-scanner', native: true)
wscanner_prog = find_program(
  wscanner.get_pkgconfig_variable('wayland_scanner'), native: true)

wl_proto_headers = []
wl_proto_src = []
foreach prot : [
  'external/wlr-layer-shell-unstable-v1.xml',
  wayland_protocols_datadir + '/stable/xdg-shell/xdg-shell.xml',
  wayland_protocols_datadir + '/unstable/xdg-output/xdg-output-unstable-v1.xml']

  wl_proto_headers += custom_target(
    prot.underscorify() + '-client-header',
    output: '@BASENAME@.h',
    input: prot,
    command: [wscanner_prog, 'client-header', '@INPUT@', '@OUTPUT@'])

  wl_proto_src += custom_target(
    prot.underscorify() + '-private-code',
    output: '@BASENAME@.c',
    input: prot,
    command: [wscanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

generate_version_sh = files('generate-version.sh')
version = custom_target(
  'generate_version',
  build_always_stale: true,
  output: 'version.h',
  command: [generate_version_sh, meson.project_version(), '@SOURCE_ROOT@', '@OUTPUT@'])

executable(
  'fuzzel',
  'application.c', 'application.h',
  'dmenu.c', 'dmenu.h',
  'fdm.c', 'fdm.h',
  'icon.c', 'icon.h',
  'log.c', 'log.h',
  'main.c',
  'match.c', 'match.h',
  'png.c', 'png-fuzzel.h',
  'prompt.c', 'prompt.h',
  'render.c', 'render.h',
  'shm.c', 'shm.h',
  'stride.h',
  'wayland.c', 'wayland.h',
  'xdg.c', 'xdg.h',
  wl_proto_src + wl_proto_headers, version,
  dependencies: [math,
                 pixman,
                 cairo,
                 libepoll,
                 libpng,
                 librsvg,
                 wayland_client,
                 wayland_cursor,
                 xkb,
                 tllist,
                 fcft],
  install: true)

install_data(
  'LICENSE', 'README.md', 'CHANGELOG.md',
  install_dir: join_paths(get_option('datadir'), 'doc', 'fuzzel'))

subdir('completions')
subdir('doc')

summary(
  {
    'Cairo': cairo.found(),
    'PNG icons': libpng.found(),
    'SVG icons': librsvg.found() and cairo.found(),
  },
  bool_yn: true
)
