#!/bin/bash
# Chromium --utility-cmd-prefix: run the utility process, log how it ended.
t=$(echo "$*" | grep -o -- '--utility-sub-type=[^ ]*')
echo "$(date +%T) start pid=$$ $t" >> /tmp/utilwrap.log
"$@"
rc=$?
if [ $rc -gt 128 ]; then how="signal $((rc - 128)) ($(kill -l $((rc - 128)) 2>/dev/null))"; else how="exit $rc"; fi
echo "$(date +%T) end pid=$$ $t $how" >> /tmp/utilwrap.log
exit $rc
