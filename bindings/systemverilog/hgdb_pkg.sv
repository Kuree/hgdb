
package hgdb_pkg;

// can be called during initial without explicitly setting
// simulator flags to load VPI entry point;
// example:
// in SystemVerilog Test bench
// initial begin

/*
* can be called during initial without explicitly setting
* simulator flags to load VPI entry point;
* example:
* in SystemVerilog Test bench
*   initial begin
      initialize_hgdb_runtime();
    end
  Then in the simulator command line, do
  xrun -sv_lib libhgdb.so hgdb_pkg.sv [other files]
  
  vcs and other simulators share similar command line switches
*/
import "DPI-C" function void initialize_hgdb_runtime_dpi();

endpackage