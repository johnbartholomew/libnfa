#!/bin/sh

# this script builds the example many times; once for each supported build variant

BUILDDIR=/memtmp

echocmd() {
	echo "$@"
	"$@"
}

build_id() {
	compiler="$1"
	lang="$2"
	with_asserts="$3"
	with_stdio="$4"
	BUILDID="$compiler-$lang"
	if test "x$with_asserts" = "xyes"; then BUILDID="$BUILDID-asserts"
	else BUILDID="$BUILDID-noasserts"; fi
	if test "x$with_stdio" = "xyes"; then BUILDID="$BUILDID-stdio"
	else BUILDID="$BUILDID-nostdio"; fi
	printf '%s' "$BUILDID"
}

check_build() {
	compiler="$1"
	lang="$2"
	with_asserts="$3"
	with_stdio="$4"

	if test "x$compiler" = "xgcc"; then BUILDCMD=gcc
	elif test "x$compiler" = "xclang"; then BUILDCMD=clang
	elif test "x$compiler" = "xscanbuild"; then BUILDCMD="scan-build clang"
	else
		printf 'unknown compiler selection' >&2
		exit 1
	fi

	if test "x$lang" = "xc89"; then BUILDCMD="$BUILDCMD -x c -std=c89 -pedantic"
	elif test "x$lang" = "xc99"; then BUILDCMD="$BUILDCMD -x c -std=c99 -pedantic"
	elif test "x$lang" = "xc11"; then BUILDCMD="$BUILDCMD -x c -std=c11 -pedantic"
	elif test "x$lang" = "xcxx98"; then BUILDCMD="$BUILDCMD -x c++ -std=c++98 -pedantic"
	elif test "x$lang" = "xcxx11"; then BUILDCMD="$BUILDCMD -x c++ -std=c++11 -pedantic"
	elif test "x$lang" = "xgnu11"; then BUILDCMD="$BUILDCMD -x c -std=gnu11"
	elif test "x$lang" = "xgnuxx11"; then BUILDCMD="$BUILDCMD -x c++ -std=gnu++11"
	else
		printf 'unknown dialect selection' >&2
		exit 1
	fi

	if test "x$with_asserts" = "xno"; then BUILDCMD="$BUILDCMD -DNFA_NDEBUG"
	elif test "x$with_asserts" != "xyes"; then
		printf 'unknown with_asserts selection' >&2
		exit 1
	fi

	if test "x$with_stdio" = "xno"; then BUILDCMD="$BUILDCMD -DNFA_NO_STDIO"
	elif test "x$with_stdio" != "xyes"; then
		printf 'unknown with_asserts selection' >&2
		exit 1
	fi

	BUILDID="$(build_id "$compiler" "$lang" "$with_asserts" "$with_stdio")"

	#echocmd $BUILDCMD -Wall -Wextra -Wshadow -O3 -g -pthread -o "$BUILDDIR"/nfa-example-"$BUILDID" example.c
	echocmd $BUILDCMD -Wall -Wextra -Wshadow -O3 -g -pthread -o "$BUILDDIR"/nfa-object-"${BUILDID}".o -c nfa.c
}

for compiler in gcc clang; do
	for lang in c89 c99 c11 cxx98 cxx11; do
		for with_asserts in yes no; do
			for with_stdio in yes no; do
				check_build $compiler $lang $with_asserts $with_stdio
			done
		done
	done
done

check_build scanbuild c89 yes yes
check_build scanbuild c89 no yes
