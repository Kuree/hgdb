#include <filesystem>
#include <iostream>

#include "engine.hh"
#include "log.hh"
#include "sim.hh"

#ifdef USE_FSDB
#include "../fsdb/fsdb.hh"
#endif

void print_usage(const std::string &program_name) {
    // detect if inside a python process
    std::string name;
    if (std::getenv("HGDB_PYTHON_PACKAGE")) {
        auto path = std::filesystem::path(program_name);
        name = path.filename();
    } else {
        name = program_name;
    }
    std::cerr << "Usage: " << name << " waveform.vcd [args]" << std::endl;
}

bool has_flag(const std::string &flag, int argc, char *argv[]) {  // NOLINT
    for (int i = 0; i < argc; i++) {
        if (argv[i] == flag) return true;
    }
    return false;
}

bool is_vcd(const std::string &filename) {
    // heuristics to detect if it's vcd
    std::ifstream stream(filename);
    std::array<char, 1> buffer = {0};
    stream.read(buffer.data(), sizeof(buffer));
    stream.close();
    return buffer[0] == '$';
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    namespace log = hgdb::log;
    std::string filename = argv[1];
    if (!std::filesystem::exists(filename)) {
        std::cerr << "Unable to find " << filename << std::endl;
        return EXIT_FAILURE;
    }

    std::unique_ptr<hgdb::waveform::WaveformProvider> db;
    if (is_vcd(filename)) {
        log::log(log::log_level::info, "Building VCD database...");

        auto has_store_db_flag = has_flag("--db", argc, argv);
        db = std::make_unique<hgdb::vcd::VCDDatabase>(filename, has_store_db_flag);
    } else {
#ifdef USE_FSDB
        db = std::make_unique<hgdb::fsdb::FSDBProvider>(filename);
#endif
    }
    if (!db) {
        log::log(log::log_level::error, "Unable to read file " + filename);
        return EXIT_FAILURE;
    }

    // notice that db will lose ownership soon. use raw pointer instead
    auto *db_ptr = db.get();

    auto vpi = std::make_unique<hgdb::replay::ReplayVPIProvider>(std::move(db));
    auto *vpi_ = vpi.get();

    // set argv
    vpi->set_argv(argc, argv);

    hgdb::replay::EmulationEngine engine(vpi.get());

    // set up the debug runtime
    log::log(log::log_level::info, "Initializing HGDB runtime...");
    auto *debugger = hgdb::initialize_hgdb_runtime_vpi(std::move(vpi), false);

    // we use hex string by default
    debugger->set_option("use_hex_str", true);

    // set the custom vpi allocator
    debugger->rtl_client()->set_vpi_allocator([vpi_]() { return vpi_->get_new_handle(); });

    // set callback on client connected
    debugger->set_on_client_connected([vpi_, debugger](hgdb::DebugDatabaseClient &table) {
        auto names = table.get_all_signal_names();
        std::vector<std::string> full_names;
        full_names.reserve(names.size());
        std::transform(
            names.begin(), names.end(), std::back_inserter(full_names),
            [debugger](const std::string &n) { return debugger->rtl_client()->get_full_name(n); });
        vpi_->build_array_table(full_names);
    });

    log::log(log::log_level::info, "Calculating design hierarchy...");
    // set the custom compute function
    // compute the mapping if definition not available
    if (!db_ptr->has_inst_definition()) {
        auto mapping_func = [db_ptr](const std::unordered_set<std::string> &instance_names)
            -> std::unordered_map<std::string, std::string> {
            auto mapping = db_ptr->compute_instance_mapping(instance_names);
            return {{mapping.first, mapping.second}};
        };

        debugger->rtl_client()->set_custom_hierarchy_func(mapping_func);
    }

    log::log(log::log_level::info, "Starting HGDB replay...");
    engine.run();
}