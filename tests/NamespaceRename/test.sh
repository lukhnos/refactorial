#!/bin/sh

set -e

/bin/cp foo.orig.h foo.h
/bin/cp foo.orig.cpp foo.cpp
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS:STRING=ON .

clang-refactor < test.yml

set +e
diff -urN foo.orig.h foo.h
sc=$?
if [[ "$sc" == "0" ]]; then
    set -e
    echo ":: ERR (no changes)"
    exit 1
fi
diff -urN foo.orig.cpp foo.cpp
sc=$?
if [[ "$sc" == "0" ]]; then
    set -e
    echo ":: ERR (no changes)"
    exit 1
fi

echo ":: OK"
## EOF ##
