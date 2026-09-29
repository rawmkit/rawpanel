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

fake_rawm = executable('fake-rawm', 'fake-rawm.c', dependencies: [x11_dep])
panel_probe = executable('panel-probe', 'panel-probe.c', dependencies: [x11_dep])
test('panel', find_program('panel.sh'), args: [rawpanel, fake_rawm, panel_probe], timeout: 15)
