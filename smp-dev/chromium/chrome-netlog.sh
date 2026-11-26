#!/bin/sh
# In the VM: headless Chromium with a NetLog, to see where loading stalls.
# usage: chrome-netlog.sh <timeout-seconds> <tag>
W=${1:-1200}; T=${2:-nl}
ulimit -v unlimited
pkill -f Xvfb; pkill -f chromium; sleep 2; rm -rf /tmp/chrn-$T
echo "start=$(date +%T)"
timeout $W chromium --headless --ipc-connection-timeout=120 --disable-gpu-watchdog \
  --user-data-dir=/tmp/chrn-$T --log-net-log=/tmp/net-$T.json --net-log-capture-mode=Default \
  --enable-logging=stderr --v=0 --dump-dom https://www.duckduckgo.com > /tmp/dom-$T.html 2>/tmp/dom-$T.err
echo "rc=$? end=$(date +%T) dom=$(wc -c < /tmp/dom-$T.html) netlog=$(wc -c < /tmp/net-$T.json)"
grep -o "<title>[^<]*</title>" /tmp/dom-$T.html | head -1
echo NETLOG-DONE
