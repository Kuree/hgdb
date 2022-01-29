#include <filesystem>

#include "../src/db.hh"
#include "cli/cli.h"
#include "cli/clilocalsession.h"
#include "cli/standaloneasioscheduler.h"
#include "fmt/format.h"

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define VERSION_STR STRINGIFY(VERSION_NUMBER)

using MainScheduler = cli::StandaloneAsioScheduler;

std::string get_program_name(char **argv) {
    std::string program_name;
    if (std::getenv("HGDB_PYTHON_PACKAGE")) {
        auto path = std::filesystem::path(program_name);
        program_name = path.filename();
    } else {
        program_name = argv[0];
    }
    return program_name;
}

bool check_db(std::unique_ptr<hgdb::SymbolTableProvider> &db, std::ostream &os) {
    if (!db) {
        cli::SetColor();
        os << "Symbol table not loaded" << std::endl;
        cli::SetNoColor();
    }
    return db != nullptr;
}

template <typename T, typename K = void>
void render(T value, std::ostream &os) {
    if constexpr (std::is_same<std::string, T>::value || std::is_arithmetic<T>::value) {
        os << value << std::endl;
    } else if constexpr (std::is_same<std::vector<K>, T>::value) {
        auto max_width = std::to_string(value.size() - 1).size();
        auto fmt_str = fmt::format("[{{0:{0}}}]: {{1}}", max_width);
        for (auto i = 0u; i < value.size(); i++) {
            os << fmt::format(fmt_str.data(), i, value[i]) << std::endl;
        }
    }
}

auto get_instance(cli::Menu &menu, std::unique_ptr<hgdb::SymbolTableProvider> &db) {
    auto sub_menu = std::make_unique<cli::Menu>("instance", "Instance data query");
    sub_menu->Insert("id", [&db](std::ostream &os, uint32_t id) {
        if (!check_db(db, os)) return;
        auto instance = db->get_instance_name(id);
        if (instance) {
            render(*instance, os);
        }
    });
    sub_menu->Insert("bp-id", [&db](std::ostream &os, uint32_t id) {
        if (!check_db(db, os)) return;
        auto instance = db->get_instance_name_from_bp(id);
        if (instance) {
            render(*instance, os);
        }
    });
    sub_menu->Insert("list", [&db](std::ostream &os) {
        if (!check_db(db, os)) return;
        auto instances = db->get_instance_names();
        render<decltype(instances), std::string>(instances, os);
    });
    return std::move(sub_menu);
}

int main(int argc, char *argv[]) {
    auto program_name = get_program_name(argv);
    auto root_menu =
        std::make_unique<cli::Menu>("hgdb", fmt::format("{0} v{1}", program_name, VERSION_STR));
    std::unique_ptr<hgdb::SymbolTableProvider> db;

    // preload the symbol table
    if (argc > 1) {
        db = hgdb::create_symbol_table(argv[1]);
    }

    root_menu->Insert("load",
                      [&db](std::ostream &, const std::string &filename) {
                          db = hgdb::create_symbol_table(filename);
                      },
                      "Load symbol table", {"path/uri"});

    root_menu->Insert(get_instance(*root_menu, db));

    cli::Cli cli(std::move(root_menu));
    MainScheduler scheduler;
    cli::CliLocalTerminalSession session(cli, scheduler, std::cout, 200);
    session.ExitAction([&db, &scheduler](std::ostream &) {
        scheduler.Stop();
        if (db) db.reset();
    });

    scheduler.Run();

    // print out new line to avoid making the prompt the same line
    std::cout << std::endl;
    return EXIT_SUCCESS;
}
