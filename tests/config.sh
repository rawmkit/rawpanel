#!/bin/sh
set -eu

rawpanel=$1
default_config=$2
tmp=${TMPDIR:-/tmp}/rawpanel-config.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
  echo "rawpanel config test failed: $*" >&2
  exit 1
}

"$rawpanel" -C -c "$default_config" || fail "packaged default was rejected"

cat >"$tmp/partial.conf" <<'EOC'
panel = { height = 31; };
EOC
"$rawpanel" -C -c "$tmp/partial.conf" || fail "partial explicit overlay was rejected"

cat >"$tmp/unknown-root.conf" <<'EOC'
panel = { height = 24; };
keyboard = { enabled = true; };
EOC
if "$rawpanel" -C -c "$tmp/unknown-root.conf" >"$tmp/out" 2>"$tmp/err"; then
  fail "unknown root setting was admitted"
fi
grep -q "unknown setting 'keyboard'" "$tmp/err" ||
  fail "unknown root setting was not explained"

cat >"$tmp/unknown-panel.conf" <<'EOC'
panel = { height = 24; shadow = true; };
EOC
if "$rawpanel" -C -c "$tmp/unknown-panel.conf" >"$tmp/out" 2>"$tmp/err"; then
  fail "unknown panel setting was admitted"
fi
grep -q "unknown setting 'shadow'" "$tmp/err" ||
  fail "unknown panel setting was not explained"

cat >"$tmp/wrong-type.conf" <<'EOC'
panel = { height = "24"; };
EOC
if "$rawpanel" -C -c "$tmp/wrong-type.conf" >"$tmp/out" 2>"$tmp/err"; then
  fail "wrong panel setting type was admitted"
fi
grep -q 'height must be an integer' "$tmp/err" ||
  fail "wrong-type failure was not explained"

cat >"$tmp/bad-color.conf" <<'EOC'
panel = { colors = { normal = { foreground = 1; }; }; };
EOC
if "$rawpanel" -C -c "$tmp/bad-color.conf" >"$tmp/out" 2>"$tmp/err"; then
  fail "non-string color was admitted"
fi
grep -q 'foreground must be a string' "$tmp/err" ||
  fail "color type failure was not explained"

cat >"$tmp/bad-button.conf" <<'EOC'
buttons = (
  { target = "title"; button = 1; action = "zoom"; mystery = 1; }
);
EOC
if "$rawpanel" -C -c "$tmp/bad-button.conf" >"$tmp/out" 2>"$tmp/err"; then
  fail "unknown button setting was admitted"
fi
grep -q "unknown setting 'mystery'" "$tmp/err" ||
  fail "button failure was not explained"

cat >"$tmp/bad-status.conf" <<'EOC'
status = { enabled = "yes"; };
EOC
if "$rawpanel" -C -c "$tmp/bad-status.conf" >"$tmp/out" 2>"$tmp/err"; then
  fail "wrong status setting type was admitted"
fi
grep -q 'enabled must be a boolean' "$tmp/err" ||
  fail "status type failure was not explained"

# Default discovery must tolerate a missing user file and use built-in/system
# policy. A relative XDG_CONFIG_HOME is ignored according to the XDG contract.
XDG_CONFIG_HOME=relative HOME= "$rawpanel" -C ||
  fail "default configuration discovery failed without a user file"

echo "rawpanel config: ok"
