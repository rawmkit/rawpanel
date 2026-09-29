project(
  'rawpanel',
  'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: ['c_std=c99', 'warning_level=3'],
  meson_version: '>=1.4.0',
)

cc = meson.get_compiler('c')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
add_project_arguments('-DVERSION="' + meson.project_version() + '"', language: 'c')

x11_dep = dependency('x11')
xft_dep = dependency('xft')
fontconfig_dep = dependency('fontconfig')
config_dep = dependency('libconfig')
alsa_dep = dependency('alsa')

subdir('src')
subdir('docs')
subdir('tests')

install_data('config/rawpanel.conf', install_dir: get_option('sysconfdir') / 'rawpanel')
install_data('LICENSE', install_dir: get_option('datadir') / 'licenses' / 'rawpanel')
install_data('README.md', 'NEWS.md', install_dir: get_option('datadir') / 'doc' / 'rawpanel')
