#!/bin/sh
gcc -std=c89 -pedantic -Wall -Wextra -O0 -g -o example example.c && exec ./example $*
