#!/bin/sh
# Runs the Infinite engine headless: it needs a display even with no window, so start a private virtual one.
# (xvfb-run is avoided: it can wait forever for the server when SIGUSR1 is ignored, as under some docker setups.)
f=$(mktemp)
Xvfb -displayfd 3 -screen 0 1280x1024x24 -nolisten tcp 3>"$f" >/dev/null 2>&1 &
xpid=$!
i=0
while [ ! -s "$f" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
[ -s "$f" ] || { echo "infinite: virtual display failed to start" >&2; kill $xpid 2>/dev/null; exit 1; }
DISPLAY=":$(cat "$f")" /opt/infinite/Infinite "$@"
rc=$?
kill $xpid 2>/dev/null
rm -f "$f"
exit $rc
