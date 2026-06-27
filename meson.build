project('vessel', 'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=c23',
    'warning_level=3',
    'buildtype=release',
    'prefix=/usr',
  ])
add_project_arguments(
  '-D_GNU_SOURCE',
  '-DVESSEL_VERSION="@0@"'.format(meson.project_version()),
  language: 'c',
)
c = meson.get_compiler('c')
deps = []
libarchive = dependency('libarchive', required: true)
deps += libarchive
libcurl = dependency('libcurl', required: true)
deps += libcurl
ncurses = dependency('ncurses', required: false)
if not ncurses.found()
  ncurses = c.find_library('ncurses', required: true)
endif
deps += ncurses
rt = c.find_library('rt', required: false)
if rt.found()
  deps += rt
endif
srcs = files(
  'src/main.c',
  'src/manifest.c',
  'src/archive.c',
  'src/namespace.c',
  'src/overlay.c',
  'src/desktop.c',
  'src/install.c',
  'src/remove.c',
  'src/run.c',
  'src/shim.c',
  'src/tui.c',
  'src/repo.c',
)

executable('vessel',
  srcs,
  dependencies: deps,
  install: true,
  install_mode: 'rwsr-xr-x',
)

meson.add_install_script('sh', '-c', 'ln -sf vessel "$DESTDIR@0@/vessel-run"'.format(join_paths(get_option('prefix'), get_option('bindir'))))
install_data('data/vessel.desktop', install_dir: join_paths(get_option('datadir'), 'applications'))
