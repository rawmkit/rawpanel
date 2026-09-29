rawpanel_sources = files(
  'main.c',
  'config.c',
  'rawm.c',
  'status/status.c',
  'status/config.c',
  'status/alsa.c',
  'status/source.c',
  'status/worker.c',
)

rawpanel = executable(
  'rawpanel',
  rawpanel_sources,
  dependencies: [x11_dep, xft_dep, fontconfig_dep, config_dep, alsa_dep],
  install: true,
)
