#!/usr/bin/env bash
set -xe

# detect the root dir
ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ROOT="$(dirname ${ROOT})"

docker pull keyiz/hgdb:lint
docker run -d --name clang-format --rm -it --mount type=bind,source=${ROOT},target=/hgdb keyiz/hgdb:lint bash
docker exec -i clang-format bash -c "cd /hgdb && clang-format -n -Werror --style=file src/*.hh src/*.cc"
docker stop clang-format
