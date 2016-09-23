#!/usr/bin/env bash

set -e

clang-tidy \
    -fix \
    -fix-errors \
    -header-filter=.* \
    --checks=readability-braces-around-statements,misc-macro-parentheses \
    src/*.c \
    -- -I. -D_GNU_SOURCE -DSYSCONFDIR=\"/etc\" \
       -DPACKAGE_VERSION=\"v0-clang-tidy\"
clang-format -style="{BasedOnStyle: llvm, IndentWidth: 4, AllowShortFunctionsOnASingleLine: None, KeepEmptyLinesAtTheStartOfBlocks: false}" -i src/*.{h,c}
