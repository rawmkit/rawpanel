status_inc = include_directories('../src')

codec_test = executable(
  'status-codec-test',
  'status-codec-test.c',
  '../src/status/status.c',
  include_directories: status_inc,
)
test('status-codec', codec_test)

alsa_test = executable(
  'status-alsa-test',
  'status-alsa-test.c',
  'status-alsa-fake.c',
  '../src/status/alsa.c',
  include_directories: status_inc,
  dependencies: [alsa_dep],
)
test('status-alsa', alsa_test)

worker_test = executable(
  'status-worker-control-test',
  'status-worker-control-test.c',
  'status-alsa-fake.c',
  '../src/status/status.c',
  '../src/status/alsa.c',
  '../src/status/source.c',
  '../src/status/worker.c',
  include_directories: status_inc,
  dependencies: [alsa_dep],
)
test('status-worker-control', worker_test)

test('config', find_program('config.sh'),
     args: [rawpanel, meson.project_source_root() / 'config' / 'rawpanel.conf'])

config_defaults_test = executable(
  'config-defaults-test',
  'config-defaults-test.c',
  '../src/config.c',
  '../src/status/config.c',
  '../src/status/status.c',
  include_directories: status_inc,
  dependencies: [config_dep],
)
test(
  'config-defaults',
  config_defaults_test,
  args: [meson.project_source_root() / 'config' / 'rawpanel.conf'],
)

fake_rawm = executable('fake-rawm', 'fake-rawm.c', dependencies: [x11_dep])
panel_probe = executable('panel-probe', 'panel-probe.c', dependencies: [x11_dep])
test('panel', find_program('panel.sh'), args: [rawpanel, fake_rawm, panel_probe], timeout: 15)

rawm_protocol_test = executable(
  'rawm-protocol-test',
  'rawm-protocol-test.c',
  '../src/rawm.c',
  include_directories: include_directories('../src'),
  dependencies: [x11_dep],
)
test(
  'rawm-protocol',
  find_program('rawm-protocol.sh'),
  args: [rawm_protocol_test],
  timeout: 15,
)
