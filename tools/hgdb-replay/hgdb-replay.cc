#include <iostream>

#include "engine.hh"
#include "sim.hh"
#include "log.hh"
#include <filesystem>

void print_usage(const std::string &program_name) {
    std::cerr << "Usage: " << program_name << " waveform.vcd [args]" << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    namespace log = hgdb::log;
    log::log(log::log_level::info, "Building VCD database...");

    std::string filename = argv[1];
    if (!std::filesystem::exists(filename)) {
        std::cerr << "Unable to find " << filename << std::endl;
        return EXIT_FAILURE;
    }

    auto db = std::make_unique<hgdb::vcd::VCDDatabase>(filename);

    // notice that db will lose ownership soon. use raw pointer instead
    auto *db_ptr = db.get();

    auto vpi = std::make_unique<hgdb::replay::ReplayVPIProvider>(std::move(db));
    // set argv
    vpi->set_argv(argc, argv);

    hgdb::replay::EmulationEngine engine(vpi.get());

    // set up the debug runtime
    log::log(log::log_level::info, "Initializing HGDB runtime...");
    auto *debugger = hgdb::initialize_hgdb_runtime_vpi(std::move(vpi), false);
    // set the custom compute function

    // compute the mapping
    auto mapping_func = [db_ptr](const std::unordered_set<std::string> &instance_names)
        -> std::unordered_map<std::string, std::string> {
      auto mapping = db_ptr->compute_instance_mapping(instance_names);
      return {{mapping.first, mapping.second}};
    };
    log::log(log::log_level::info, "Calculating design hierarchy...");
    debugger->rtl_client()->set_custom_hierarchy_func(mapping_func);

    log::log(log::log_level::info, "Starting HGDB replay...");
    engine.run();
}