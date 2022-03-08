# How to set up simulators with hgdb

This documentation details the command line options you need to load hgdb runtime into your simulator of choice.
For most cases, you need to make sure that the simulator can find `libhgdb.so`. The easiest way is to add the
directory containing the library into `LD_LIBRARY_PATH`, if you are using windows.

## Cadence® Xcelium™
```bash
xrun [commands] -access +r -loadvpi libhgdb.so:initialize_hgdb_runtime
```

Notice if you want to set values when you debug, you need to use `-access +rw` to allow write access.

## Synopsys VCS®

```bash
vcs [commands] -debug_acc+all -load libhgdb.so
```

Notice that `-debug_acc+all` slows down the simulation significantly. For most cases, `-debug_access+class` is
sufficient, which gives you minimal simulation overhead.

## Mentor Questa®
```bash
vsim [flags] -pli libghdb.so
```
Questa is fairly simple since all you need is point to the library.

## Verilator

Verilator is a little tedious since it is not specification-compliant and thus not event-driven.

First, we need to generate the verilated files with extra VPI flags

```bash
erilator [flags] --vpi ${path_to_libhgdb.so}
```

In addition, most signals should be labeled as public, otherwise breakpoints and frame
inspection will not work. An easy way is to use `--public-flat-rw`
flag when invoking `verilator`. In addition to the flags, we need add following code to the test bench:

- Forward declare the runtime call:
    ```C++
    namespace hgdb {
        void initialize_hgdb_runtime_cxx();
    }
    ```
- At the beginning of the test bench code:
    ```C++
    hgdb::initialize_hgdb_runtime_cxx();
    ```
    Also make sure ``argc`` and ``argv`` are properly passed to verilator:

    ```C++
    Verilated::commandArgs(argc, argv);
    ```
- At each posedge of the clock, we need to call specific callback:
    ```C++
      VerilatedVpi::callCbs(cbNextSimTime);
    ```

You can check out this [test bench](https://github.com/Kuree/hgdb/blob/master/tests/generators/vectors/test_circt_tb.cc)
for more details.

## Icarus Verilog

Icarus Verilog only takes shared library with ``.vpi`` extension. As a result,
it is a good idea to simply symbolic link `libhgdb.so` to `libhgdb.vpi` in the
current working directory. When you run the compiled circuit with `vvp`, add the following command:

```bash
vvp -M. -mlibhgdb [commands]
```