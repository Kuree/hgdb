#!/usr/bin/env bash
set -xe

ROOT="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
ROOT="$(dirname "$(dirname "$(dirname "${ROOT}")")")"
docker run -it --rm -d --name manylinux -v "${ROOT}":/hgdb keyiz/manylinux bash
# python changes its python tag...
docker exec -i manylinux bash -c 'cd /hgdb/bindings/python && for PYBIN in cp36-cp36m cp37-cp37m cp38-cp38; do /opt/python/${PYBIN}/bin/python setup.py bdist_wheel; done'
docker exec -i manylinux bash -c 'cd /hgdb/bindings/python&& for WHEEL in dist/*.whl; do auditwheel repair "${WHEEL}"; done'
docker stop manylinux
