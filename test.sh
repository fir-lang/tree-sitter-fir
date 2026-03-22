#!/bin/bash

shopt -s globstar

FILE="${1:-../fir}"

pass=0
fail=0

GREEN='\033[0;32m'
RED='\033[0;31m'
RESET='\033[0m'

if [ -f "$FILE" ]; then
    files=("$FILE")
else
    files=($FILE/**/*.fir)
fi

for f in "${files[@]}"; do
    output=$(timeout 5 tree-sitter parse "$f" 2>&1)
    exit_code=$?

    if [ $exit_code -ne 0 ]; then
        echo -e "${RED}FAIL${RESET} $f"
        fail=$((fail + 1))
        continue
    fi

    # Check that the parse tree covers the entire file. A partial parse
    # (scanner can't tokenize something) can silently produce a truncated tree
    # with exit code 0.
    file_lines=$(wc -l < "$f")
    parse_end_row=$(echo "$output" | head -1 | sed -n 's/.*- \[\([0-9]*\),.*/\1/p')

    if [ -n "$parse_end_row" ] && [ "$parse_end_row" -lt "$((file_lines - 1))" ]; then
        echo -e "${RED}FAIL${RESET} $f (partial parse: tree ends at row $parse_end_row, file has $file_lines lines)"
        fail=$((fail + 1))
        continue
    fi

    echo -e "${GREEN}PASS${RESET} $f"
    pass=$((pass + 1))
done

echo "Pass: $pass, Fail: $fail"
