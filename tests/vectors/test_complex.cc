#include "Vtest_complex.h"
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

void advance_clock(Vtest_complex &dut, int increment = 1, bool call_update = false) {
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
    dut.clk = ~dut.clk;
    dut.eval();
    VerilatedVpi::callValueCbs();  // required to call callbacks
    VerilatedVpi::callTimedCbs();
    main_time += increment;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Vtest_complex dut;
    Verilated::internalsDump();  // See scopes to help debug
    // Create an instance of our module under test
    hgdb::initialize_hgdb_runtime_cxx();

    dut.clk = 0;

    for (int i = 0; i < 4; i++) {
        advance_clock(dut, 10, true);
        advance_clock(dut, 10);
    }
    dut.final();

    exit(EXIT_SUCCESS);
}
