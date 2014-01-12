#!/bin/sh

a5=aaaaa
a26="$a5$a5$a5$a5$a5"a
aq5='a?a?a?a?a?'
aq26="$aq5$aq5$aq5$aq5$aq5"'a?'

pattern="$aq26$a26"
input="$a26"

printf 'Pattern: "%s"\nInput: "%s"\n' "$pattern" "$input"

build_flags="-DNFA_NO_STDIO -O0 -fno-inline"
printf 'Testing libnfa compiled with %s:\n' "$build_flags"
gcc -std=c89 -Wall -Wextra $build_flags -g -o example example.c && time ./example "$pattern" "$input"

printf 'Testing grep:\n'
time printf '%s\n' "$input" | grep -oE -e "^$pattern"

printf 'Testing python:\n'
time python3 -c 'import sys; import re; print(re.match(sys.argv[1], sys.argv[2]))' "$pattern" "$input"
