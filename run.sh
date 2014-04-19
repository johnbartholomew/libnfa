#!/bin/sh
gcc -std=c89 -pedantic -Wall -Wextra -Wno-unused-function -O0 -g -o example example.c && exec ./example "$@"
