#!/usr/bin/env bash
set -xe

# detect the root dir
ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ROOT="$(dirname "${ROOT}")"

docker pull keyiz/hgdb:test

docker run -d --name ci-test --rm -it --mount type=bind,source=${ROOT},target=/hgdb keyiz/hgdb:test bash
# build everything
docker exec -i ci-test bash -c "cd /hgdb && mkdir -p build && cd build && cmake .. && make -j"
# install tests dependencies
docker exec -i ci-test bash -c "bash /hgdb/bindings/python/scripts/install.sh"
docker exec -i ci-test bash -c "pip install kratos"
docker exec -i ci-test bash -c "cd /hgdb && pytest tests/ build/tests -v"
docker stop ci-test
# test other bindings
docker run -d --name ci-test --rm -it --mount type=bind,source=${ROOT},target=/hgdb keyiz/hgdb:tcl bash
docker exec -i ci-test bash -c "pip install /hgdb/bindings/python/dist/*.whl"
docker exec -i ci-test bash -c "cd /hgdb/ && pytest tests/bindings/test_tcl.py -v"
