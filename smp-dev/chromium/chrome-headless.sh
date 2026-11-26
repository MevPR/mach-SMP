#!/bin/sh
T=${1:-hl}
ulimit -v unlimited
timeout 2400 chromium --headless --ipc-connection-timeout=120 --disable-gpu-watchdog --user-data-dir=/tmp/chrh-$T --dump-dom https://www.duckduckgo.com > /tmp/dom-$T.html 2>/tmp/dom-$T.err; echo "rc=$?"
wc -c < /tmp/dom-$T.html; grep -o "<title>[^<]*</title>" /tmp/dom-$T.html | head -1; grep -c -i duckduckgo /tmp/dom-$T.html
echo HEADLESS-DONE
