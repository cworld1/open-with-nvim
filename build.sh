#!/usr/bin/env bash
set -e

# Build resources first, then link GUI executable.
windres -i app.rc -o app.res -O coff

gcc -std=c23 -Wall -Wextra -O2 -municode -mwindows -o nvim.exe \
    main.c app.res

echo "Build completed: nvim.exe"
