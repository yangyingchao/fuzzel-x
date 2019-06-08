project('f00sel', 'c',
        version: '1.1.0',
        license: 'MIT',
        meson_version: '>=0.0.0',
        default_options: [
          'c_std=c11',
          'warning_level=1',
          'werror=true',
          'b_ndebug=if-release'])

is_debug_build = get_option('buildtype').startswith('debug')

version = '"@0@"'.format(meson.project_version())

sh = find_program('sh', native: true)
git = find_program('git', required: false, native: true)

if git.found()
  commit_hash = run_command(
    [sh.path(), '-c',
     '@0@ --git-dir="$MESON_SOURCE_ROOT/.git" describe --always --tags'.format(
       git.path())])

  branch = run_command(
    [sh.path(), '-c',
     '@0@ --git-dir="$MESON_SOURCE_ROOT/.git" rev-parse --abbrev-ref HEAD'.format(
       git.path())])

  if commit_hash.returncode() == 0 and branch.returncode() == 0
    version = '"@0@ (" __DATE__ ", branch \'@1@\')"'.format(
      commit_hash.stdout().strip(), branch.stdout().strip())
  endif
endif

add_project_arguments(
  ['-D_GNU_SOURCE',
   '-DF00SEL_VERSION=@0@'.format(version)] +
  (is_debug_build ? ['-D_DEBUG'] : []),
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

executable(
  'f00sel',
  'main.c',
  'font.c', 'font.h',
  'icon.c', 'icon.h',
  'render.c', 'render.h',
  'shm.c', 'shm.h',
  'log.c', 'log.h',
  'xdg.c', 'xdg.h',
  'application.c', 'application.h',
  'prompt.h',
  'tllist.h',
  wl_proto_src + wl_proto_headers,
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

subdir('completions')
subdir('doc')
