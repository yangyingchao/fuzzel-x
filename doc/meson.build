sh = find_program('sh', native: true)
scdoc = dependency('scdoc', native: true)
scdoc_prog = find_program(scdoc.get_pkgconfig_variable('scdoc'), native: true)
gzip = find_program('gzip', native: true)

foreach man_src : ['f00sel.scd']
  base = man_src.split('.')[-2]
  out = '@0@.1.gz'.format(base)

  custom_target(
    out,
    output: out,
    input: man_src,
    command: [sh, '-c', '@0@ < @INPUT@ | @1@ > doc/@2@'.format(
      scdoc_prog.path(), gzip.path(), out)],
    install: true,
    install_dir: '@0@/man1'.format(get_option('mandir')))
endforeach
