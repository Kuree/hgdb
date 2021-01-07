#include "Vmod.h"
#include "verilated.h"
#include "verilated_vpi.h"  // Required to get definitions

void initialize_hgdb_runtime();

vluint64_t main_time = 0;       // Current simulation time

double sc_time_stamp () {       // Called by $time in Verilog
    return main_time;           // converts to double, to match
    // what SystemC does
}

void eval(Vmod &dut, int increment = 1) {
    dut.eval();
    VerilatedVpi::callValueCbs(); // required to call callbacks
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
    Vmod dut;
    // Verilated::internalsDump();  // See scopes to help debug
    // Create an instance of our module under test
    initialize_hgdb_runtime();

    for (int i = 0; i < 4; i++) {
        dut.in = i + 1;
        eval(dut);
        dut.clk = 1;
        eval(dut, 5);
        dut.clk = 0;
        eval(dut, 5);
    }
    dut.final();

    exit(EXIT_SUCCESS);
}