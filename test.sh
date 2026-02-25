#!/bin/bash

shopt -s globstar

FILE="${1:-../fir}"

pass=0
fail=0

GREEN='\033[0;32m'
RED='\033[0;31m'
RESET='\033[0m'

for f in $FILE/**/*.fir; do
    if timeout 5 tree-sitter parse "$f" > /dev/null 2>&1; then
        echo -e "${GREEN}PASS${RESET} $f"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} $f"
        fail=$((fail + 1))
    fi
done

echo "Pass: $pass, Fail: $fail"
