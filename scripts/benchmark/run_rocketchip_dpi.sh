#!/usr/bin/env bash

set -xe

ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# notice that you need to change the following command to have VCS in the $PATH
# I will use the one I have
docker run -it --rm -d --name rocket-chip-dpi -v /home/keyi/workspace/cad:/cad -v "${ROOT}":/data keyiz/rocket-chip bash
# clone the repo
docker exec -it rocket-chip-dpi bash -c "git clone https://github.com/chipsalliance/rocket-chip && cd rocket-chip/ && git submodule update --init"
# build jar file
docker exec -it rocket-chip-dpi bash -c "cd /rocket-chip/vsim/ && make /rocket-chip/rocketchip.jar"
# build verilog
docker exec -it rocket-chip-dpi bash -c "cd /rocket-chip/vsim && make verilog"
# copy DPI C file and let VCS to compile it
docker cp "${ROOT}/hgdb_debug_dpi.c" rocket-chip-dpi:/
# rewrite RTL
docker cp "${ROOT}/insert_dpi.py" rocket-chip-dpi:/
docker exec -it rocket-chip-dpi bash -c "python3 insert_dpi.py /rocket-chip/vsim/generated-src/freechips.rocketchip.system.DefaultConfig.v /rocket-chip/vsim/generated-src/freechips.rocketchip.system.DefaultConfig.v"
# apply patch
docker cp "${ROOT}/dpi.patch" rocket-chip-dpi:/
docker exec -it rocket-chip-dpi bash -c "cd rocket-chip && git apply /dpi.patch"
# build vsim, change this based on how you load your vcs
docker exec -it rocket-chip-dpi bash -c "source /cad/load.sh && cd /rocket-chip/vsim && make"
rm -rf dpi.log
for app in mm spmv mt-vvadd median multiply qsort towers vvadd dhrystone mt-matmul; do
  /usr/bin/time -a -o dpi.log -f "%E" sh -c "echo '${app}' >> dpi.log; docker exec -it rocket-chip-dpi bash -c 'source /cad/load.sh && cd /rocket-chip/vsim && ${ENV} DEBUG_PERF_COUNT_NAME=${app} make output/${app}.riscv.out'"
done

docker stop rocket-chip-dpi
