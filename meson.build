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

mir_release_args = []
if not get_option('debug')
  mir_release_args += ['-DNDEBUG', '-DMIR_NO_GEN_DEBUG=1']
endif

mir_lib = static_library(
  'mir', mir_sources,
  c_args: mir_warn_args + mir_release_args,
  install: false,
)

c2mir_size_args = []
if cc.get_argument_syntax() == 'msvc'
  c2mir_size_args = ['/O1']
elif cc.has_argument('-Oz')
  c2mir_size_args = ['-Oz']
else
  c2mir_size_args = ['-Os']
endif

c2mir_lib = static_library(
  'c2mir', files('c2mir/c2mir.c'),
  c_args: mir_warn_args + mir_release_args + c2mir_size_args,
  install: false,
)

mir_dep = declare_dependency(
  link_with: [c2mir_lib, mir_lib],
  include_directories: include_directories('.'),
)
