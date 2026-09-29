# Shared Xvfb lifecycle for X11 integration tests.
#
# The test suite deliberately avoids Xvfb -displayfd here.  Older X servers do
# not provide it, while fixed display numbers make parallel test execution race
# with other tests or an existing server.  An atomic directory claim gives each
# test a private display number without adding another X11 utility dependency.

test_xvfb_pid=
test_xvfb_lock=
test_xvfb_display=

test_xvfb_stop()
{
  if [ -n "$test_xvfb_pid" ]; then
    kill "$test_xvfb_pid" 2>/dev/null || true
    wait "$test_xvfb_pid" 2>/dev/null || true
    test_xvfb_pid=
  fi

  if [ -n "$test_xvfb_lock" ]; then
    rmdir "$test_xvfb_lock" 2>/dev/null || true
    test_xvfb_lock=
  fi

  test_xvfb_display=
}

test_xvfb_start()
{
  test_xvfb_state_dir=$1
  test_xvfb_screen=${2:-800x600x24}
  test_xvfb_second_screen=${3:-}
  test_xvfb_lock_root=${TMPDIR:-/tmp}/rawm-xvfb-displays

  mkdir -p "$test_xvfb_lock_root" || return 1

  test_xvfb_display_number=90
  while [ "$test_xvfb_display_number" -lt 190 ]; do
    test_xvfb_candidate_lock=$test_xvfb_lock_root/$test_xvfb_display_number
    if mkdir "$test_xvfb_candidate_lock" 2>/dev/null; then
      if [ -e "/tmp/.X${test_xvfb_display_number}-lock" ] ||
         [ -e "/tmp/.X11-unix/X${test_xvfb_display_number}" ]; then
        rmdir "$test_xvfb_candidate_lock" 2>/dev/null || true
        test_xvfb_display_number=$((test_xvfb_display_number + 1))
        continue
      fi

      test_xvfb_candidate_display=:$test_xvfb_display_number
      test_xvfb_log=$test_xvfb_state_dir/xvfb.log
      test_xvfb_server=${RAWM_TEST_XVFB:-Xvfb}
      test_xvfb_xwininfo=${RAWM_TEST_XWININFO:-xwininfo}
      if [ -n "$test_xvfb_second_screen" ]; then
        "$test_xvfb_server" "$test_xvfb_candidate_display" \
          -screen 0 "$test_xvfb_screen" \
          -screen 1 "$test_xvfb_second_screen" \
          +xinerama -nolisten tcp >"$test_xvfb_log" 2>&1 &
      else
        "$test_xvfb_server" "$test_xvfb_candidate_display" \
          -screen 0 "$test_xvfb_screen" \
          -nolisten tcp >"$test_xvfb_log" 2>&1 &
      fi
      test_xvfb_candidate_pid=$!

      test_xvfb_tries=0
      while [ "$test_xvfb_tries" -lt 100 ]; do
        if DISPLAY=$test_xvfb_candidate_display "$test_xvfb_xwininfo" -root >/dev/null 2>&1; then
          test_xvfb_pid=$test_xvfb_candidate_pid
          test_xvfb_lock=$test_xvfb_candidate_lock
          test_xvfb_display=$test_xvfb_candidate_display
          DISPLAY=$test_xvfb_candidate_display
          export DISPLAY
          return 0
        fi

        if ! kill -0 "$test_xvfb_candidate_pid" 2>/dev/null; then
          break
        fi

        test_xvfb_tries=$((test_xvfb_tries + 1))
        sleep 0.02
      done

      if kill -0 "$test_xvfb_candidate_pid" 2>/dev/null; then
        kill "$test_xvfb_candidate_pid" 2>/dev/null || true
      fi
      wait "$test_xvfb_candidate_pid" 2>/dev/null || true
      rmdir "$test_xvfb_candidate_lock" 2>/dev/null || true

      echo "Xvfb startup log:" >&2
      cat "$test_xvfb_log" >&2 || true
      return 1
    fi

    test_xvfb_display_number=$((test_xvfb_display_number + 1))
  done

  echo "no free Xvfb display in :90..:189" >&2
  return 1
}
