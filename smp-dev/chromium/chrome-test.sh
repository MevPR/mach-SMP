#!/bin/sh
# In the VM: start Chromium under Xvfb on duckduckgo.com, take a screenshot.
ulimit -v unlimited
# usage: chrome-test.sh <wait-seconds> <tag>
W=${1:-600}; T=${2:-run}
FLAGS="--ipc-connection-timeout=1200 --disable-gpu-watchdog --no-first-run --no-default-browser-check --user-data-dir=/tmp/chr-$T --window-size=1280,860 --window-position=0,0 --log-net-log=/tmp/net-$T.json $EXTRA"
pkill -f Xvfb; pkill -f chromium; sleep 2; rm -rf /tmp/chr-$T /tmp/.X1-lock
Xvfb :1 -screen 0 1280x900x24 -nolisten tcp > /tmp/xvfb-$T.log 2>&1 &
sleep 10
echo "nproc=$(nproc) start=$(date +%T)"
DISPLAY=:1 chromium --ozone-platform=x11 $FLAGS https://www.duckduckgo.com > /tmp/chrome-$T.log 2>&1 &
CP=$!
i=0; while [ $i -lt $W ]; do sleep 30; i=$((i+30)); kill -0 $CP 2>/dev/null || { echo "chromium exited after ${i}s"; break; }
  [ $((i % 300)) -eq 0 ] && DISPLAY=:1 import -window root /tmp/shot-$T-$i.png 2>/dev/null; done
echo "procs: $(ps -e -o args | grep -c '[c]hromium')"
DISPLAY=:1 import -window root /tmp/shot-$T.png 2>/tmp/import-$T.err && ls -la /tmp/shot-$T.png
tail -5 /tmp/chrome-$T.log
echo "end=$(date +%T)"; echo CHROME-TEST-DONE
