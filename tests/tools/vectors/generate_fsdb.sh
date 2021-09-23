#!/usr/bin/env bash

if [ "$#" -eq 1 ]; then
    vcs -sverilog -lca -kdb -debug_access+all "$1"
    ./simv
else
    echo "$0 file.sv"
    exit -1
fi
