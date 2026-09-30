#!/bin/sh
set -eu
case $0 in */*) test_dir=${0%/*} ;; *) test_dir=. ;; esac
. "$test_dir/support/xvfb.sh"
rawpanel=$1
fake=$2
tray_client=$3
tmp=${TMPDIR:-/tmp}/rawpanel-tray-lifetime-test.$$
mkdir -p "$tmp"
fake_pid= panel_pid= tray_pid=
cleanup() {
  [ -z "$tray_pid" ] || kill "$tray_pid" 2>/dev/null || true
  [ -z "$panel_pid" ] || kill "$panel_pid" 2>/dev/null || true
  [ -z "$fake_pid" ] || kill "$fake_pid" 2>/dev/null || true
  [ -z "$tray_pid" ] || wait "$tray_pid" 2>/dev/null || true
  [ -z "$panel_pid" ] || wait "$panel_pid" 2>/dev/null || true
  [ -z "$fake_pid" ] || wait "$fake_pid" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
skip() { echo "tray lifetime tests: skipped ($*)"; exit 77; }
fail() { echo "tray lifetime test failed: $*" >&2; [ ! -f "$tmp/panel.log" ] || cat "$tmp/panel.log" >&2; exit 1; }
command -v Xvfb >/dev/null 2>&1 || skip "Xvfb not found"
test_xvfb_start "$tmp" || skip "Xvfb did not become ready"
cat >"$tmp/rawpanel.conf" <<'EOC'
panel = {
  font = "fixed";
  position = "top";
  height = 24;
  padding = 4;
  item_spacing = 2;
  section_spacing = 4;
  cell_margin = 2;
  indicator_size = 1;
  tags = "occupied";
};
systray = { enabled = true; spacing = 2; icon_size = 16; };
status = { enabled = false; };
buttons = ();
EOC
"$fake" >"$tmp/fake.out" 2>"$tmp/fake.err" & fake_pid=$!
n=0
while [ ! -s "$tmp/fake.out" ]; do
  n=$((n+1)); [ "$n" -lt 100 ] || fail "fake rawm did not start"
  sleep 0.02
done
start_panel() {
  : >"$tmp/panel.log"
  "$rawpanel" -c "$tmp/rawpanel.conf" >"$tmp/panel.log" 2>&1 & panel_pid=$!
  n=0
  while ! "$tray_client" lifecycle >/dev/null 2>&1; do
    n=$((n+1)); [ "$n" -lt 100 ] || fail "system tray did not become ready"
    kill -0 "$panel_pid" 2>/dev/null || fail "rawpanel exited while starting"
    sleep 0.02
  done
  kill -0 "$panel_pid" 2>/dev/null || fail "rawpanel exited during tray readiness probe"
}

start_panel
"$tray_client" survive >"$tmp/tray.out" 2>"$tmp/tray.err" & tray_pid=$!
n=0
while ! grep -q '^ready$' "$tmp/tray.out" 2>/dev/null; do
  n=$((n+1)); [ "$n" -lt 100 ] || fail "survival client did not dock"
  kill -0 "$tray_pid" 2>/dev/null || { cat "$tmp/tray.err" >&2 || true; fail "survival client exited before manager loss"; }
  sleep 0.02
done
kill -KILL "$panel_pid"
wait "$panel_pid" 2>/dev/null || true
panel_pid=
if ! wait "$tray_pid"; then
  tray_pid=
  cat "$tmp/tray.err" >&2 || true
  fail "embedded tray client did not survive abrupt manager loss"
fi
tray_pid=

start_panel
"$tray_client" race >/dev/null 2>&1 || fail "could not issue disappearing-client race"
sleep 0.10
kill -0 "$panel_pid" 2>/dev/null || fail "disappearing tray client killed rawpanel"

echo "tray lifetime tests: ok"
