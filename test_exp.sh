#!/bin/sh

a27='aaaaa''aaaaa''aaaaa''aaaaa''aaaaa''aa'
aq27='a?a?a?a?a?''a?a?a?a?a?''a?a?a?a?a?''a?a?a?a?a?''a?a?a?a?a?''a?a?'

pattern="$aq27$a27"
input="$a27"

printf "Pattern: '%s'\nInput: '%s'\n" "$pattern" "$input"

build_flags="-DNFA_NO_STDIO -O0 -fno-inline"
printf 'Testing libnfa compiled with %s:\n' "$build_flags"
gcc -std=c89 -pedantic -Wall -Wextra $build_flags -g \
        -o example example.c && time ./example "$pattern" "$input"

printf 'Testing grep:\n'
time printf '%s\n' "$input" | grep -oE -e "^$pattern"

printf 'Testing perl:\n'
time perl -E 'say "match" if $ARGV[1] =~ m/^$ARGV[0]/' "$pattern" "$input"

printf 'Testing python:\n'
time python3 -c 'import sys; import re; print(re.match(sys.argv[1], sys.argv[2]))' "$pattern" "$input"
