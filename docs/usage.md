# How to install and use hgdb

## Install via pip
The easiest way to get started is to install the compiled shared object
via ``pip``. To do so, simply type

```
pip install libhgdb
```

You can find the download shared library using the following one-liner

```bash
python -c "import pkgutil; print(pkgutil.get_loader('libhgdb').path)"
```

You can copy it ot symbolic link to places you want to use.

## Compile from source

To compile it from source, you need a C++20 compatible compiler, such as
`gcc-10` or `clang-10`. Make sure that git submodules are properly cloned.

```bash
git clone --recurse-submodules -j8 https://github.com/Kuree/hgdb
cd hgdb
mkdir build && cd build && cmake ..
make hgdb -j
```
You should see the compiled shared library in `build/src/`


## Runtime library settings

You can change the runtime settings using plus-args when invoking the simulator. Here is
a short list of options you can change:

- `+DEBUG_PORT=num`, where ``num`` is the port number. By default, this is `8888`
- `+DEBUG_LOG=1`, enable the debugging log. Useful when debugging the behavior of the
  runtime

There are several predefined environment variables one can use to debug the runtime. It
is not recommended for production usage:

- `DEBUG_DISABLE_BLOCKING`: when present, will disable the initial blocking. As a result,
  the simulator will start execution without user's explicit "start" or "continue"
  command.
- `DEBUG_BREAKPOINT#=filename:line_num@[condition]`: where `#` counts from 0. The runtime will
  query the predefined breakpoints starting from 0 and stops if corresponding environment
  variable name not found. `condition`` is optional.
- `DEBUG_PERF_COUNT`: when present, the system will collect performance information. Only valid
  when the library is build with `-DPERF_COUNT=ON` when invoking `cmake`.
- `DEBUG_PERF_COUNT_LOG`: when set, the system will dump the performance data into the set value
  instead of cout;

## Usage
To use hgdb, see [simulators](simulator.md) and [debuggers](debugger.md).
By design, hgdb only offers an API for other tools to interact with.


## Replay tool
`hgdb-replay` is a tool that allows you to replay captured trace files. By default, it only supports VCD-based traces
files. However, if you have `VCS` in your environment, the build system will automatically detect and compile it with
`FSDB` support.

During replay, users can use reverse debugging to rewind time using the supported debuggers. Use `-h` to see the
available options.

## VCD Rewrite
`hgdb-vcd-rewrite` is a tool that rewrites the trace files given a symbol table. It is used to refactor the waveforms
using source-level symbols. Due to the limitation of VCD format, structural signals are grouped as a module instance.
Use `-h` to see the available options.

## DB query tool
`hgdb-db` is a tool to query the symbol table. It aims to provide an easy way to debug different kinds of symbol
table in a human-readable form. It uses menu-based system to navigate.

To see available options, simple do

```
hgdb-db symbol-table-file-name.db
```

Then type `help` in the prompt.
