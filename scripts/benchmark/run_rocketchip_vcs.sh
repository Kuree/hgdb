#!/usr/bin/env bash

set -xe


ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# notice that you need to change the following command to have VCS in the $PATH
# I will use the one I have
docker run -it --rm -d --name rocket-chip-vcs -v /home/keyi/workspace/cad:/cad keyiz/rocket-chip bash
# clone the repo
docker exec -it rocket-chip-vcs bash -c "git clone https://github.com/chipsalliance/rocket-chip && cd rocket-chip/ && git submodule update --init"
# apply patch
docker cp "${ROOT}/vcs.patch" rocket-chip-vcs:/
docker exec -it rocket-chip-vcs bash -c "cd rocket-chip && git apply /vcs.patch"
# build verilog
docker exec -it rocket-chip-vcs bash -c "cd /rocket-chip/vsim && make verilog"
# build vsim, change this based on how you load your vcs
docker exec -it rocket-chip-vcs bash -c "source /cad/load.sh && cd /rocket-chip/vsim && make"

# loop through all applications and measure the time
rm -rf vcs.log
for app in mm spmv mt-vvadd median multiply qsort towers vvadd dhrystone mt-matmul; do
  /usr/bin/time -a -o vcs.log -f "%E" sh -c "echo '${app}' >> vcs.log; docker exec -it rocket-chip-vcs bash -c 'source /cad/load.sh && cd /rocket-chip/vsim && make output/${app}.riscv.out'"
done

docker stop rocket-chip-vcs
