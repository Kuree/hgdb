#!/usr/bin/env bash
set -xe

# currently only supports linux so far. MacOS build is just so much pain
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT=$(dirname "${DIR}")
docker run -it -d --rm --name manylinux -v "${ROOT}":/hgdb keyiz/manylinux bash
docker exec -i manylinux bash -c 'cd /hgdb && python setup.py bdist_wheel'
docker exec -i manylinux bash -c 'cd /hgdb && auditwheel repair dist/* -w wheels'
# use the fix wheel script
docker exec -i manylinux bash -c 'pip install wheeltools'
docker exec -i manylinux bash -c 'cd /hgdb && mkdir -p wheelhouse && python scripts/fix_wheel.py wheels/*.whl -w wheelhouse'
docker stop manylinux