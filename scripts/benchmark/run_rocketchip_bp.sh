#!/usr/bin/env bash

set -xe

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# we use 8 breakpoints whose condition is always false (1 bit signal = 2)
export DEBUG_BREAKPOINT0=ALU.scala:84:25@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_r_bits_last==2
export DEBUG_BREAKPOINT1=ALU.scala:84:44@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_w_ready==2
export DEBUG_BREAKPOINT2=ALU.scala:84:35@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_w_valid==2
export DEBUG_BREAKPOINT3=ALU.scala:84:18@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_w_bits_last==2
export DEBUG_BREAKPOINT4=ALU.scala:84:74@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_b_ready==2
export DEBUG_BREAKPOINT5=ALU.scala:85:28@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_b_valid==2
export DEBUG_BREAKPOINT6=ALU.scala:85:18@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_ar_ready==2
export DEBUG_BREAKPOINT7=ALU.scala:88:25@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_axi4buf_out_ar_valid==2

BP_ENV_TEXT=(
    "DEBUG_BREAKPOINT0=${DEBUG_BREAKPOINT0} "
    "DEBUG_BREAKPOINT1=${DEBUG_BREAKPOINT1} "
    "DEBUG_BREAKPOINT2=${DEBUG_BREAKPOINT2} "
    "DEBUG_BREAKPOINT3=${DEBUG_BREAKPOINT3} "
    "DEBUG_BREAKPOINT4=${DEBUG_BREAKPOINT4} "
    "DEBUG_BREAKPOINT5=${DEBUG_BREAKPOINT5} "
    "DEBUG_BREAKPOINT6=${DEBUG_BREAKPOINT6} "
    "DEBUG_BREAKPOINT7=${DEBUG_BREAKPOINT7} ")
export BP_ENV=${BP_ENV_TEXT[*]}
export DEBUG_PERF_COUNT_LOG="/data/hgdb_perf.log"

source "${ROOT}"/run_rocketchip_hgdb.sh
