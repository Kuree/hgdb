#!/usr/bin/env bash
set -xe

# detect the root dir
ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ROOT="$(dirname ${ROOT})"

docker pull keyiz/hgdb:test

docker run -d --name ci-test --rm -it --mount type=bind,source=${ROOT},target=/hgdb keyiz/hgdb:test bash
docker exec -i ci-test bash -c "cd /hgdb && pytest tests/ -v"
docker stop ci-test
