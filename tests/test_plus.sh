#!/bin/sh
# Run from the repository root: make -f Makefile.plus test
set -eu
shell="$(pwd)/shellforge-plus"
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT HUP INT TERM
cd "$temp"
export SHELLFORGE_HISTORY="$temp/.shellforge_history"
check() {
    label=$1 input=$2 expected=$3 code=$4
    actual_code=0
    printf '%s\n' "$input" | "$shell" >actual 2>errors || actual_code=$?
    printf '%s' "$expected" >expected
    if [ "$actual_code" -ne "$code" ] || ! cmp -s actual expected; then
        printf 'FAIL: %s (status %s, expected %s)\n' "$label" "$actual_code" "$code"
        cat errors
        exit 1
    fi
    printf 'PASS: %s\n' "$label"
}
check quotes 'printf "<%s><%s><%s>" "two words" '\''literal|text'\'' ""' '<two words><literal|text><>' 0
check escape 'printf "%s" two\ words' 'two words' 0
check printf_escape 'printf "first\nsecond\n"' 'first
second
' 0
check pipeline 'printf "abc"|tr a-z A-Z|cat' 'ABC' 0
check redirection 'printf "one" > data
printf "two" >> data
cat < data' 'onetwo' 0
check cd 'mkdir "new dir"
cd "new dir"
printf "ok" > marker
cat marker' 'ok' 0
check missing_command 'shellforge_command_that_does_not_exist' '' 127
check missing_input 'cat < missing-file' '' 1
check syntax 'printf x |' '' 2
check unmatched_quote 'printf "unfinished' '' 2
check duplicate_redirection 'cat > a > b' '' 2
check builtin_pipeline 'cd / | cat' '' 2
check builtin_redirection 'help > help.txt' '' 2
check invalid_exit 'exit invalid' '' 2
check explicit_exit 'exit 7
printf unreachable' '' 7
check last_status 'false
exit' '' 1
check recovery 'printf x |
printf recovered' 'recovered' 0
check empty '' '' 0
# A producer larger than a pipe buffer catches waiting-before-fork deadlocks.
check large_pipeline 'seq 1 100000 | wc -l' '100000
' 0
printf 'All functional checks passed.\n'
