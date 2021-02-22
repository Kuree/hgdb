|HGDB Logo|

hgdb is a flexible hardware debugging framework. It offers runtime
APIs to interact with the simulator.

Core features
-------------

hgdb is designed to be versatile and provides an abstraction to
facilitate hardware debugging. It offers the following features:

- Breakpoints, including step-over and conditional breakpoint.
- Frame/context reconstruction with complex data types.
- Full reverse debugging in replay mode, and limited capability in interactive
  debugging.
- Set signal values in interactive debugging
- Symbol table and query. No RTL modification required.

Supported Simulators
~~~~~~~~~~~~~~~~~~~~

The simulators listed below have been tested in regression tests.
Theoretically hgdb can run on any Verilog/SystemVerilog specification
compliant simulator.

- Cadence® Xcelium™
- Synopsys VCS®
- Verilator
- Icarus Verilog

Supported Generator Frameworks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We are working on passes to extract symbol tables from different
generator frameworks. The list below will be growing!

- Chisel/Firrtl, via `hgdb-firrtl`_.
- Kratos, native support.
- Hand-written Verilog/SystemVerilog, via hgdb-rtl (work in progress).

Usage
-----

The easiest way to get started is to install the compiled shared object
via ``pip``. To do so, simply type

.. code-block::

   pip install libhgdb

You can find the download shared library using the following one-liner

.. code-block:: bash

   python -c "import pkgutil; print(pkgutil.get_loader('libhgdb').path)"

You can copy it ot symbolic link to places you want to use.

Compile from source
~~~~~~~~~~~~~~~~~~~

To compile it from source, you need a C++20 compatible compiler, such as
``gcc-10`` or ``clang-10``. Make sure that git submodules are properly cloned.

.. code-block:: bash

   git clone --recurse-submodules -j8 https://github.com/Kuree/hgdb
   cd hgdb
   mkdir build && cd build && cmake ..
   make hgdb -j

You should see the compiled shared library in ``build/src/``

How to use it with simulators
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You need to provide specific flags to the simulator in order to load the
runtime. Notice that in most cases you need to make sure that the
simulator can find ``libhgdb.so``. The easiest way is to invoke commands
with ``LD_LIBRARY_PATH=${hgdb_lib_path}$``, where ``${hgdb_lib_path}``
is the directory containing ``libhgdb.so``. Here are some examples on
how to use it with different simulators.

- Cadence® Xcelium™

  .. code-block:: bash

    xrun [commands] -access +rw -loadvpi libhgdb.so:initialize_hgdb_runtime

- Synopsys VCS®

  .. code-block:: bash

    vcs [commands] -debug_acc+all -load libhgdb.so

- Verilator

  Verilator is a little bit tedious since it is not specification-compliant.
  ``verilator [flags] --vpi ${path_to_libhgdb.so}``
  In addition, most signals should be labeled as public, otherwise breakpoints and frame
  inspection will not work. An easy way is to use ``--public-flat-rw``
  flag. In addition to the flags, we need add following code to the test bench:

  - Forward declare the runtime call:

    .. code-block:: C++

        namespace hgdb {
        void initialize_hgdb_runtime_cxx();
        }

  - At the beginning of the test bench code:

    .. code-block:: C++

      hgdb::initialize_hgdb_runtime_cxx();

    Also make sure ``argc`` and ``argv`` are properly passed to verilator:

    .. code-block:: C++

      Verilated::commandArgs(argc, argv);

  - At each posedge of the clock, we need to call specific callback:

    .. code-block:: C++

      VerilatedVpi::callCbs(cbNextSimTime);

    You can check out this `example test bench`_ for more details.

- Icarus Verilog

  Icarus Verilog only takes shared library with ``.vpi`` extension. As a result,
  it is a good idea to simply symbolic link `libhgdb.so` to `libhgdb.vpi` in the
  current working directory.
  When you run the compiled circuit with `vvp`, add the following command:

  .. code-block:: bash

    vvp -M. -mlibhgdb [commands]

Runtime command-line arguments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
You can change the runtime settings using plus-args when invoking the simulator. Here is
a short list of options you can change:

- ``-DEBUG_PORT=num``, where ``num`` is the port number. By default this is ``888```
- ``-DEBUG_LOG=1``, enable the debugging log. Useful when debugging the behavior of the
  runtime


Which debugger to use
~~~~~~~~~~~~~~~~~~~~~

hgdb offers several open-sourced debuggers:

-  Visual Studio Code Debugger Extension
-  ``gdb``-style debugger

You can check out the debuggers `here`_.


Reverse-debugging
~~~~~~~~~~~~~~~~~
hgdb supports full reverse-debugging via trace file. Users can forward
and backward any time, with breakpoint support. This is achieved by a
trace replay tool that implements hgdb's compatibility layer. The tool,
``hgdb-replay``, is shipped with `libhgdb` package. To use it, simply do

.. code-block:: bash

  hgdb-replay waveform.vcd [args]

where ``[args]`` are optional arguments passed to the debug runtime.

Source-level waveform
~~~~~~~~~~~~~~~~~~~~~

hgdb also supports source-level waveform by rewriting existing waveform against
the symbol table. The rewritten waveform will produce source-level
constructs, such as ``Bundle`` and arrays. Currently only VCD format is
supported. The rewrite tool ``hgdb-rewrite-vcd`` is shipped with ``libhgdb``
package.

.. code-block:: bash

   $ hgdb-rewrite-vcd <original.vcd> <debug.db> <new.vcd>

Symbol table generation
-----------------------

The symbol table used by hgdb is designed to be compiler-friendly and
language-independent. Hardware generator framework developers should
check this `document`_ out to see more details.

Available language bindings
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Below shows a list of language bindings offered by hgdb and their implementation status

-  C/C++: ``creation`` ``query`` ``runtime``
-  Python: ``creation`` ``query``
-  SystemVerilog: ``runtime``
-  tcl: ``query``

.. _hgdb-firrtl: https://github.com/Kuree/hgdb-firrtl
.. |HGDB Logo| image:: https://github.com/Kuree/files/raw/master/images/hgdb-logo-header.svg
.. _here: https://github.com/Kuree/hgdb-debugger
.. _document: https://github.com/Kuree/hgdb/blob/master/docs/README.md
.. _example test bench: https://github.com/Kuree/hgdb/blob/master/tests/vectors/test_set_value_tb.cc
