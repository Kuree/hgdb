#include <filesystem>
#include <iostream>

#include "argparse/argparse.hpp"
#include "engine.hh"
#include "log.hh"
#include "sim.hh"

#ifdef USE_FSDB
#include "../fsdb/fsdb.hh"
#endif

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define VERSION_STR STRINGIFY(VERSION_NUMBER)

std::optional<argparse::ArgumentParser> get_args(int argc, char **argv) {
    argparse::ArgumentParser program("HGDB Replay", VERSION_STR);
    std::string program_name;
    if (std::getenv("HGDB_PYTHON_PACKAGE")) {
        auto path = std::filesystem::path(program_name);
        program_name = path.filename();
    } else {
        program_name = argv[0];
    }
    // make the program name look nicer
    argv[0] = const_cast<char *>(program_name.c_str());

    program.add_argument("filename").help("Waveform file in either VCD or FSDB format").required();
    // optional argument for vcd
    program.add_argument("--db").implicit_value(true).default_value(false);
    // we can specify the port as well instead of changing it in the env by hand
    program.add_argument("--port", "-p")
        .help("Debug port")
        .default_value<uint16_t>(0)
        .scan<'d', uint16_t>();
    program.add_argument("--debug").implicit_value(true).default_value(false);

    try {
        program.parse_args(argc, argv);
        return program;
    } catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return std::nullopt;
    }
}

bool is_vcd(const std::string &filename) {
    // heuristics to detect if it's vcd
    std::ifstream stream(filename);
    std::array<char, 1> buffer = {0};
    stream.read(buffer.data(), sizeof(buffer));
    stream.close();
    return buffer[0] == '$';
}

void set_port(hgdb::replay::ReplayVPIProvider *vpi, uint16_t port) {
    // only try to override the env when the port is not 0
    if (port > 0) {
        auto str = "+DEBUG_PORT=" + std::to_string(port);
        vpi->add_argv(str);
    }
}

void set_debug_log(hgdb::replay::ReplayVPIProvider *vpi, bool enable) {
    if (enable) {
        auto constexpr str = "+DEBUG_LOG";
        vpi->add_argv(str);
    }
}

// NOLINTNEXTLINE
int main(int argc, char *argv[]) {
    auto program = get_args(argc, argv);
    if (!program) {
        return EXIT_FAILURE;
    }
    namespace log = hgdb::log;
    auto filename = program->get("filename");

    std::unique_ptr<hgdb::waveform::WaveformProvider> db;
    if (is_vcd(filename)) {
        log::log(log::log_level::info, "Building VCD database...");

        auto has_store_db_flag = program->get<bool>("--db");
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
    // set port if necessary
    set_port(vpi.get(), program->get<uint16_t>("--port"));
    // we use plus args
    set_debug_log(vpi.get(), program->get<bool>("--debug"));

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