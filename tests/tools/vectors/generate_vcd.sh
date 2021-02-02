#!/usr/bin/env bash

if [ "$#" -eq 1 ]; then
    xrun "$1" -access +r
else
    for file in *.sv; do
        xrun "${file}" -access +r
    done
fi
