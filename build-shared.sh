#!/bin/sh

gcc \
 -fvisibility=hidden \
 -shared \
 -std=c99 -pedantic -Wall -Wshadow \
 -Os -g -fPIC -pthread \
 -DNFA_API='__attribute__((visibility("default")))' \
 -Wl,-soname=libnfa.so.0 \
 -Wl,--version-script=libnfa.map \
 -olibnfa.debug.so.0.0 \
 nfa.c
strip --strip-unneeded -olibnfa.so.0.0 libnfa.debug.so.0.0
