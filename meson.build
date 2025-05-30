project('fuzzel', ['c', 'cpp'],
        version: '1.12.0',
        license: 'MIT',
        meson_version: '>=0.58.0',
        default_options: [
          'c_std=c18',
          'cpp_std=c++17',
          'warning_level=1',
          'werror=true',
          'b_ndebug=if-release'])

is_debug_build = get_option('buildtype').startswith('debug')
cc = meson.get_compiler('c')

add_project_arguments(
  ['-D_GNU_SOURCE'] +
  (is_debug_build
   ? ['-D_DEBUG']
   : [cc.get_supported_arguments('-fno-asynchronous-unwind-tables')]),
  language: 'c',
)

if cc.has_function('memfd_create',
                   args: ['-D_GNU_SOURCE=200809L'],
                   prefix: '#include <sys/mman.h>')
  add_project_arguments('-DMEMFD_CREATE', language: 'c')
endif

# Compute the relative path used by compiler invocations.
source_root = meson.current_source_dir().split('/')
build_root = meson.global_build_root().split('/')
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
threads = [dependency('threads'), cc.find_library('stdthreads', required: false)]
fontconfig = dependency('fontconfig')
pixman = dependency('pixman-1')
cairo = dependency('cairo', required: get_option('enable-cairo'))
libepoll = dependency('epoll-shim', required: false)
wayland_protocols = dependency('wayland-protocols', version: '>=1.32')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
xkb = dependency('xkbcommon')
system_nanosvg = cc.find_library('nanosvg', required: get_option('system-nanosvg'))
system_nanosvgrast = cc.find_library('nanosvgrast', required: get_option('system-nanosvg'))

tllist = dependency('tllist', version: '>=1.0.1', fallback: 'tllist')
fcft = dependency('fcft', version: ['>=3.0.0', '<4.0.0'], fallback: 'fcft')

if cairo.found()
  add_project_arguments('-DFUZZEL_ENABLE_CAIRO=1', language: 'c')
endif

if get_option('png-backend') == 'libpng'
  libpng = dependency('libpng')
  add_project_arguments('-DFUZZEL_ENABLE_PNG_LIBPNG=1', language: 'c')
else
  libpng = declare_dependency()
endif

if cairo.found() and get_option('svg-backend') == 'librsvg'
  librsvg = dependency('librsvg-2.0')
  nanosvg = declare_dependency()
  svg_backend = 'librsvg'
  add_project_arguments('-DFUZZEL_ENABLE_SVG_LIBRSVG=1', language: 'c')
elif get_option('svg-backend') == 'nanosvg'
  if system_nanosvg.found() and system_nanosvgrast.found()
    nanosvg = declare_dependency(
      dependencies: [system_nanosvg, system_nanosvgrast]
    )
    svg_backend = 'nanosvg (system)'
  else
    nanosvg = declare_dependency(
      sources: ['nanosvg.c', '3rd-party/nanosvg/src/nanosvg.h',
                'nanosvgrast.c', '3rd-party/nanosvg/src/nanosvgrast.h'],
      include_directories: '.',
      dependencies: math)
    svg_backend = 'nanosvg (bundled)'
  endif
  add_project_arguments('-DFUZZEL_ENABLE_SVG_NANOSVG=1', language: 'c')
  librsvg = declare_dependency()
else
  svg_backend = 'none'
  librsvg = declare_dependency()
  nanosvg = declare_dependency()
endif

wayland_protocols_datadir = wayland_protocols.get_variable('pkgdatadir')

wscanner = dependency('wayland-scanner', native: true)
wscanner_prog = find_program(
  wscanner.get_variable('wayland_scanner'), native: true)

wl_proto_headers = []
wl_proto_src = []

wl_proto_xml = [
  'external/wlr-layer-shell-unstable-v1.xml',
  wayland_protocols_datadir + '/stable/xdg-shell/xdg-shell.xml',
  wayland_protocols_datadir + '/unstable/xdg-output/xdg-output-unstable-v1.xml',
  wayland_protocols_datadir + '/staging/xdg-activation/xdg-activation-v1.xml',
  wayland_protocols_datadir + '/staging/cursor-shape/cursor-shape-v1.xml',
  wayland_protocols_datadir + '/unstable/tablet/tablet-unstable-v2.xml',
  wayland_protocols_datadir + '/stable/viewporter/viewporter.xml',
  wayland_protocols_datadir + '/staging/fractional-scale/fractional-scale-v1.xml',
  wayland_protocols_datadir / 'unstable/primary-selection/primary-selection-unstable-v1.xml',
]

foreach prot : wl_proto_xml
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

env = find_program('env', native: true)
generate_version_sh = files('generate-version.sh')
version = custom_target(
  'generate_version',
  build_always_stale: true,
  output: 'version.h',
  command: [env, 'LC_ALL=C', generate_version_sh, meson.project_version(), '@CURRENT_SOURCE_DIR@', '@OUTPUT@'])

executable(
  'fuzzel',
  'application.c', 'application.h',
  'char32.c', 'char32.h',
  'clipboard.c', 'clipboard.h',
  'column.c', 'column.h',
  'config.c', 'config.h',
  'debug.c', 'debug.h',
  'dmenu.c', 'dmenu.h',
  'event.c', 'event.h',
  'fdm.c', 'fdm.h',
  'icon.c', 'icon.h',
  'key-binding.c', 'key-binding.h',
  'log.c', 'log.h',
  'macros.h',
  'main.c',
  'match.c', 'match.h',
  'path.c', 'path.h',
  'plugin.c', 'plugin.h',
  'png.c', 'png-fuzzel.h',
  'prompt.c', 'prompt.h',
  'render.c', 'render.h',
  'shm.c', 'shm.h',
  'stride.h',
  'uri.c', 'uri.h',
  'wayland.c', 'wayland.h',
  'xdg.c', 'xdg.h',
  'xmalloc.c', 'xmalloc.h',
  'xsnprintf.c', 'xsnprintf.h',
  wl_proto_src + wl_proto_headers, version,
  dependencies: [math,
                 threads,
                 pixman,
                 fontconfig,
                 cairo,
                 libepoll,
                 libpng,
                 librsvg,
                 nanosvg,
                 wayland_client,
                 wayland_cursor,
                 xkb,
                 tllist,
                 fcft],
  install: true)

install_data(
  'fuzzel.ini',
  install_dir: join_paths(get_option('sysconfdir'), 'xdg', 'fuzzel'))
install_data(
  'LICENSE', 'README.md', 'CHANGELOG.md',
  install_dir: join_paths(get_option('datadir'), 'doc', 'fuzzel'))

subdir('completions')
subdir('doc')
subdir('plugins')

summary(
  {
    'Cairo': cairo.found(),
    'PNG icons': get_option('png-backend'),
    'SVG icons': svg_backend,
  },
  bool_yn: true
)
