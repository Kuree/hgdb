#!/usr/bin/env bash
set -xe

# detect the root dir
ROOT="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
ROOT="$(dirname "${ROOT}")"

CWD="$(pwd)"
cd "${ROOT}"
python setup.py bdist_wheel
WHEEL_NAME="$(ls dist/*.whl)"
pip install "${WHEEL_NAME}"[client]

cd "${CWD}"
