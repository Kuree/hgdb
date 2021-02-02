#include <iostream>

#include "engine.hh"
#include "sim.hh"

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
    // notice that db will lose ownership soon. use raw pointer instead
    auto *db_ptr = db.get();
    // compute the mapping
    auto mapping_func = [db_ptr](const std::unordered_set<std::string> &instance_names)
        -> std::unordered_map<std::string, std::string> {
        auto mapping = db_ptr->compute_instance_mapping(instance_names);
        return {{mapping.first, mapping.second}};
    };

    auto vpi = std::make_unique<hgdb::replay::ReplayVPIProvider>(std::move(db));
    // set argv
    vpi->set_argv(argc, argv);

    hgdb::replay::EmulationEngine engine(vpi.get());

    // set up the debug runtime
    auto *debugger = hgdb::initialize_hgdb_runtime_vpi(std::move(vpi), false);
    // set the custom compute function
    debugger->rtl_client()->set_custom_hierarchy_func(mapping_func);

    engine.run();
}