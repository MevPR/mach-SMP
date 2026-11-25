# usage: corefull.sh <exe-in-sysroot> <core>   (inside container)
R=/hb/sym2508/root
ARGS=""
python3 /hb/libmap.py $2 $R > /tmp/libmap.txt
while read f base n; do
  [ "$f" = "/$1" ] && continue
  ARGS="$ARGS -ex \"add-symbol-file $R$f -o $base\""
done < /tmp/libmap.txt
eval gdb -q -batch -ex "\"set osabi GNU/Linux\"" -ex "\"set sysroot $R\"" -ex "\"set debug-file-directory $R/usr/lib/debug\"" \
  -ex "\"file $R/$1\"" -ex "\"core $2\"" $ARGS -ex "\"thread apply all bt 30\"" 2>&1 | \
  grep -v "^\[New LWP\|Current thread\|^warning\|Cannot access memory\|add symbol table\|^Reading symbols\|No shared libraries\|^(y or n)\|^#0  0x.* in ?? ()$"
