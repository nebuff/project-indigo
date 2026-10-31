project('indigo', 'c', version: '0.1.1')

gnome = import('gnome')

gtk4 = dependency('gtk4')
webkitgtk = dependency('webkitgtk-6.0')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')

resources = gnome.compile_resources(
  'indigo',
  'data/indigo.gresource.xml',
  source_dir: 'data',
  c_name: 'indigo',
)

executable(
  'indigo',
  'src/main.c',
  'src/browser.c',
  'src/history.c',
  'src/look.c',
  'src/menu.c',
  'src/omnibox.c',
  'src/pages.c',
  'src/search_history.c',
  'src/tab.c',
  'src/util.c',
  resources,
  dependencies: [gtk4, webkitgtk, cairo, pangocairo],
  install: true,
)

install_data(
  'data/icons/hicolor/scalable/apps/com.projectindigo.Browser.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

install_data(
  'data/com.projectindigo.Browser.desktop',
  install_dir: get_option('datadir') / 'applications',
)
