#!/usr/bin/env bash

for file in *.sv; do
    xrun ${file} -access +r
done
