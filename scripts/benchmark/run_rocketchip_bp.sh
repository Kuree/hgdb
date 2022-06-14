#!/usr/bin/env bash

set -xe

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

export BP_ENV="DEBUG_BREAKPOINT0=WidthWidget.scala:149@TestHarness.ldut.subsystem_sbus.coupler_to_port_named_mmio_port_axi4_auto_tl_in_a_ready==2"
export DEBUG_PERF_COUNT_LOG="hgdb.log"

source "${ROOT}"/run_rocketchip_hgdb.sh
