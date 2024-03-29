#!/usr/bin/env bash

set -xe

ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# notice that you need to change the following command to have VCS in the $PATH
# I will use the one I have
docker run -it --rm -d --name rocket-chip-hgdb -v /home/keyi/workspace/cad:/cad -v "${ROOT}":/data keyiz/rocket-chip bash
# clone the repo
docker exec -it rocket-chip-hgdb bash -c "git clone https://github.com/chipsalliance/rocket-chip && cd rocket-chip/ && git submodule update --init"
# build jar file
docker exec -it rocket-chip-hgdb bash -c "cd /rocket-chip/vsim/ && make /rocket-chip/rocketchip.jar"
# install firrtl2hgdb converter so that we can create symbol table for benchmarking
docker exec -it rocket-chip-hgdb bash -c "apt-get update && apt-get install -y python3-pip && pip3 install hgdb[all]"
if [ -z "${BP_ENV}" ]
then
  docker exec -it rocket-chip-hgdb bash -c "pip3 install libhgdb"
else
  # need to install it from scratch
  docker exec -it rocket-chip-hgdb bash -c "apt-get install -y cmake g++"
  docker exec -it rocket-chip-hgdb bash -c "git clone https://github.com/Kuree/hgdb"
  docker exec -it rocket-chip-hgdb bash -c "cd hgdb && git submodule update --init --recursive"
  docker exec -it rocket-chip-hgdb bash -c "cd hgdb && PERF=1 python3 setup.py bdist_wheel && pip3 install dist/*.whl"
  # set perf count on
  BP_ENV="${BP_ENV} DEBUG_PERF_COUNT=1 DEBUG_PERF_COUNT_LOG=${DEBUG_PERF_COUNT_LOG}"
fi
# install hgdb-firrtl
docker exec -it rocket-chip-hgdb bash -c "git clone https://github.com/Kuree/hgdb-firrtl"
docker exec -it rocket-chip-hgdb bash -c "/hgdb-firrtl/bin/install /rocket-chip/rocketchip.jar"
# apply patch
docker cp "${ROOT}/hgdb.patch" rocket-chip-hgdb:/
docker exec -it rocket-chip-hgdb bash -c "cd rocket-chip && git apply /hgdb.patch"
# build verilog
docker exec -it rocket-chip-hgdb bash -c "cd /rocket-chip/vsim && make verilog"
# convert toml to hgdb
docker exec -it rocket-chip-hgdb bash -c "toml2hgdb /rocket-chip/debug.toml /rocket-chip/debug.db"
# build vsim, change this based on how you load your vcs
docker exec -it rocket-chip-hgdb bash -c "source /cad/load.sh && cd /rocket-chip/vsim && make"
# set the hgdb env variable to automatically start simulation without blocking
export ENV="DEBUG_DATABASE_FILENAME=/rocket-chip/debug.db DEBUG_DISABLE_BLOCKING=1 ${BP_ENV}"
rm -rf hgdb.log
for app in mm spmv mt-vvadd median multiply qsort towers vvadd dhrystone mt-matmul; do
  /usr/bin/time -a -o hgdb.log -f "%E" sh -c "echo '${app}' >> hgdb.log; docker exec -it rocket-chip-hgdb bash -c 'source /cad/load.sh && cd /rocket-chip/vsim && ${ENV} DEBUG_PERF_COUNT_NAME=${app} make output/${app}.riscv.out'"
done

docker stop rocket-chip-hgdb
