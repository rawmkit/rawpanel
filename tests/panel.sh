#!/bin/sh
set -eu
case $0 in */*) test_dir=${0%/*} ;; *) test_dir=. ;; esac
. "$test_dir/support/xvfb.sh"
rawpanel=$1
fake=$2
probe=$3
tmp=${TMPDIR:-/tmp}/rawpanel-panel-test.$$
mkdir -p "$tmp"
fake_pid= panel_pid=
cleanup() {
  [ -z "$panel_pid" ] || kill "$panel_pid" 2>/dev/null || true
  [ -z "$fake_pid" ] || kill "$fake_pid" 2>/dev/null || true
  [ -z "$panel_pid" ] || wait "$panel_pid" 2>/dev/null || true
  [ -z "$fake_pid" ] || wait "$fake_pid" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
skip() { echo "panel tests: skipped ($*)"; exit 77; }
fail() { echo "panel test failed: $*" >&2; [ ! -f "$tmp/panel.log" ] || cat "$tmp/panel.log" >&2; exit 1; }
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
systray = { enabled = false; };
status = { enabled = false; };
buttons = ();
EOC
"$fake" >"$tmp/fake.out" 2>"$tmp/fake.err" & fake_pid=$!
n=0
while [ ! -s "$tmp/fake.out" ]; do n=$((n+1)); [ "$n" -lt 100 ] || fail "fake rawm did not start"; sleep 0.02; done
"$rawpanel" -c "$tmp/rawpanel.conf" >"$tmp/panel.log" 2>&1 & panel_pid=$!
wait_dock() {
  expected=$1 n=0
  while :; do
    line=$($probe dock 2>/dev/null || true)
    if [ -n "$line" ]; then
      mapped=$(printf '%s\n' "$line" | awk '{print $6}')
      [ "$mapped" = "$expected" ] && { printf '%s\n' "$line"; return 0; }
    fi
    n=$((n+1)); [ "$n" -lt 150 ] || fail "dock did not reach mapped=$expected"
    kill -0 "$panel_pid" 2>/dev/null || fail "rawpanel exited"
    sleep 0.02
  done
}
line=$(wait_dock 1)
set -- $line
[ "$2 $3 $4 $5 $6 $7 $8" = "0 0 800 24 1 24 0" ] || fail "unexpected dock: $line"
"$probe" supported _RAWPANEL_CMD_TOGGLE_VISIBILITY ||
  fail "toggle-visibility capability was not advertised"
if "$probe" supported _RAWPANEL_CMD_VOLUME_MUTE; then
  fail "volume capability was advertised without an active status worker"
fi
"$probe" toggle
wait_dock 0 >/dev/null
"$probe" toggle
wait_dock 1 >/dev/null
kill "$fake_pid"; wait "$fake_pid" 2>/dev/null || true; fake_pid=
n=0
while kill -0 "$panel_pid" 2>/dev/null; do n=$((n+1)); [ "$n" -lt 100 ] || fail "rawpanel survived rawm authority loss"; sleep 0.02; done
wait "$panel_pid" 2>/dev/null || true; panel_pid=
echo "panel tests: ok"
