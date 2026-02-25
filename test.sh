#!/bin/bash

shopt -s globstar

FILE="${1:-../fir/tests}"

pass=0
fail=0

for f in $FILE/**/*.fir; do
    echo $f
    if timeout 5 tree-sitter parse "$f" > /dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done

echo "Pass: $pass, Fail: $fail"
