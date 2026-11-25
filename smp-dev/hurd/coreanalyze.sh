# usage: coreanalyze.sh <exe-in-sysroot> <core>
R=/hb/sym2508/root
gdb -q -batch -ex "set osabi GNU/Linux" -ex "set sysroot $R" -ex "set debug-file-directory $R/usr/lib/debug" \
  -ex "set solib-search-path $R/lib/x86_64-gnu:$R/usr/lib/x86_64-gnu:$R/lib" \
  -ex "file $R/$1" -ex "core $2" ${3:+-ex "add-symbol-file $R/usr/lib/x86_64-gnu/libc.so.0.3 -o $3"} ${4:+-ex "add-symbol-file $R/usr/lib/x86_64-gnu/libhurduser.so.0.3 -o $4"} -ex "info sharedlibrary" -ex "info threads" \
  -ex "thread apply all bt 25" 2>&1 | grep -v "OS ABI\|Attempting to continue\|^warning: Unable\|^$"
