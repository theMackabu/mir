project('mir', 'c')

cc = meson.get_compiler('c')
cpu = host_machine.cpu_family()
mir_sources = files('mir.c', 'mir-gen.c')

mir_warn_args = cc.get_supported_arguments([
  '-fsigned-char',
  '-Wno-unused-function',
  '-Wno-unused-variable',
  '-Wno-unused-parameter',
  '-Wno-sign-compare',
  '-Wno-missing-field-initializers',
  '-Wno-implicit-fallthrough',
  '-Wno-pedantic',
  '-Wno-format-nonliteral',
  '-Wno-cast-function-type',
  '-Wno-double-promotion',
  '-Wno-conversion',
])

mir_lib = static_library(
  'mir',
  mir_sources,
  c_args: mir_warn_args,
  install: false,
)

mir_dep = declare_dependency(
  link_with: mir_lib,
  include_directories: include_directories('.'),
)
