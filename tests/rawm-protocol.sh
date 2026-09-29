#!/bin/sh
set -eu
case $0 in */*) test_dir=${0%/*} ;; *) test_dir=. ;; esac
. "$test_dir/support/xvfb.sh"

test_bin=$1
tmp=${TMPDIR:-/tmp}/rawpanel-rawm-protocol.$$
mkdir -p "$tmp"
cleanup()
{
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
command -v Xvfb >/dev/null 2>&1 || { echo "rawpanel rawm protocol: skipped (Xvfb unavailable)"; exit 77; }
test_xvfb_start "$tmp" || { echo "rawpanel rawm protocol: skipped (Xvfb unusable)"; exit 77; }
"$test_bin"
