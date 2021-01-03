#!/usr/bin/env bash
set -xe

# detect the root dir
ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ROOT="$(dirname ${ROOT})"

docker pull keyiz/hgdb:lint
docker run -d --name clang-tidy --rm -it --mount type=bind,source=${ROOT},target=/hgdb keyiz/hgdb:lint bash
docker exec -i clang-tidy bash -c "cd hgdb && mkdir -p build && cd build && cmake -DUSE_CLANG_TIDY=TRUE .."
docker exec -i clang-tidy bash -c "cd hgdb/build && make -j2 hgdb"
docker stop clang-tidy
