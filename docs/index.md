![hgdb logo](https://github.com/Kuree/files/raw/master/images/hgdb-logo-header.svg)
# hgdb: Hardware Generator Debugger
This documentation describes the design philosophy and usages of hgdb.

## What is hgdb?
hgdb is designed to be versatile and provides an abstraction to
facilitate hardware debugging. It offers the following features:

- Breakpoints, including step-over and conditional breakpoint.
- Frame/context reconstruction with complex data types.
- Full reverse debugging in replay mode, and limited capability in interactive
  debugging.
- Set signal values in interactive debugging
- Symbol table and query. No RTL modification required.
- High-level synthesis (HLS) support.

hgdb is not:

- An actual debugger. Although the naming is a little misleading, hgdb offers a unified debugger interface. 
  See supported debuggers [here](https://github.com/Kuree/hgdb-debugger).
  Unless you want to debug with `curl` commands, it's highly recommended using a supported debugger.
- A framework that helps you maintain symbol table in your hardware compiler.
  hgdb only offers means to create and query symbol table.

## Supported simulators
The simulators listed below have been tested in regression tests.
Theoretically hgdb can run on any Verilog/SystemVerilog specification
compliant simulator.

- Cadence® Xcelium™
- Synopsys VCS®
- Mentor Questa®
- Verilator
- Icarus Verilog


## Supported hardware generator frameworks

- Chisel/Firrtl, via [hgdb-firrtl](https://github.com/Kuree/hgdb-firrtl).
- CIRCT, native support. Current requires patch [here](https://github.com/Kuree/circt).
- Kratos, native support.
- LegUp (HLS), experimental support, via [hgdb-legup](https://github.com/Kuree/hgdb-legup).
- Xilinx Vitis, via [hgdb-vitis](https://github.com/Kuree/hgdb-vitis)
- Hand-written Verilog/SystemVerilog, via [hgdb-rtl](https://github.com/Kuree/hgdb-rtl).

## Supported debuggers

- VSCode extension: [hgdb-vscode](https://marketplace.visualstudio.com/items?itemName=keyiz.hgdb-vscode)
- Console: [hgdb-debugger](https://pypi.org/project/hgdb-debugger/)

