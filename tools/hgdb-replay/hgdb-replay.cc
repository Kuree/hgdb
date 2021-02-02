#include "engine.hh"
#include "sim.hh"
#include <iostream>


void print_usage(const std::string &program_name) {
    std::cerr << "Usage: " << program_name << " waveform.vcd [args]" << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    std::string filename = argv[1];
    auto db = std::make_unique<hgdb::vcd::VCDDatabase>(filename);
    auto vpi = std::make_unique<hgdb::replay::ReplayVPIProvider>(std::move(db));
    // set argv
    vpi->set_argv(argc, argv);

    hgdb::replay::EmulationEngine engine(vpi.get());

    // set up the debug runtime
    hgdb::initialize_hgdb_runtime_vpi(std::move(vpi), true);

    engine.run();
}