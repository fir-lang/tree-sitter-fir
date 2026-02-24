#!/bin/bash

shopt -s globstar

FILE="${1:-../fir/tests}"

pass=0
fail=0

for f in $FILE/**/*.fir; do
    echo $f
    result=$(timeout 5 tree-sitter parse "$f" 2>&1)
    if echo "$result" | grep -qF '(ERROR'; then
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done

echo "Pass: $pass, Fail: $fail"
