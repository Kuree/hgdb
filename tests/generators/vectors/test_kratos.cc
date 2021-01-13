#include "Vmod.h"
#include "verilated.h"
#include "verilated_vpi.h"  // Required to get definitions

namespace hgdb {
void initialize_hgdb_runtime_cxx();
}

vluint64_t main_time = 0;  // Current simulation time

double sc_time_stamp() {  // Called by $time in Verilog
    return main_time;     // converts to double, to match
    // what SystemC does
}

void eval(Vmod &dut, int increment = 1, bool call_update = false) {
    if (call_update) {
        // notice that nextSimTime has to be called *right before* you trigger
        // posedge clock
        // to allow proper updates especially from the IO stimulus
        // call eval before the clock posedge, as follows:
        // dut.in = 1;
        // dut.eval();
        // callCBs();
        // dut.clk = 1;
        // eval();
        // force verilator to handle cbNextSimTime
        VerilatedVpi::callCbs(cbNextSimTime);
    }
    dut.eval();
    VerilatedVpi::callValueCbs();  // required to call callbacks
    VerilatedVpi::callTimedCbs();
    main_time += increment;
}

void reset(Vmod &dut) {
    dut.rst = 0;
    eval(dut);
    dut.rst = 1;
    eval(dut);
    dut.rst = 0;
    eval(dut);
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Vmod dut;
    Verilated::internalsDump();  // See scopes to help debug
    // Create an instance of our module under test
    hgdb::initialize_hgdb_runtime_cxx();

    reset(dut);

    for (int i = 0; i < 4; i++) {
        dut.in = i + 1;
        eval(dut);
        dut.clk = 1;
        eval(dut, 10, true);
        dut.clk = 0;
        eval(dut, 10);
    }
    dut.final();

    exit(EXIT_SUCCESS);
}