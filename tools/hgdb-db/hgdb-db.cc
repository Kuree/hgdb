#include <filesystem>

#include "../src/db.hh"
#include "../src/util.hh"
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

class ColorScope {
public:
    ColorScope() { cli::SetColor(); }

    ~ColorScope() { cli::SetNoColor(); }
};

struct AssignmentInfo {
    hgdb::BreakPoint bp;
    std::string name;
    std::string cond;
};

const std::string &get_indent(uint32_t indent) {
    static std::vector<std::string> cache;
    if (indent >= cache.size()) {
        auto start = cache.size();
        cache.resize(indent + 1);
        for (auto i = start; i < cache.size(); i++) {
            std::stringstream ss;
            for (auto j = 0; j < i; j++) {
                ss << ' ';
            }
            cache[i] = ss.str();
        }
    }
    return cache[indent];
}

template <typename T>
constexpr bool is_basic_type() {
    return std::is_same<std::string, T>::value || std::is_arithmetic<T>::value;
}

template <typename T, typename K = void>
void render(T &&value, std::ostream &os, uint32_t indent = 0) {
    if constexpr (is_basic_type<T>()) {
        os << get_indent(indent) << value << std::endl;
    } else if constexpr (std::is_same<std::vector<K>, T>::value) {
        auto max_width = std::to_string(value.size() - 1).size();
        if constexpr (is_basic_type<K>()) {
            auto fmt_str = fmt::format("[{{0:{0}}}]: {{1}}", max_width);
            for (auto i = 0u; i < value.size(); i++) {
                os << get_indent(indent) << fmt::format(fmt_str.data(), i, value[i]) << std::endl;
            }
        } else {
            auto fmt_str = fmt::format("[{{0:{0}}}]: {{1}}", max_width);
            auto ind = fmt::format(fmt_str, max_width, "").size();
            auto new_indent = indent + ind;
            fmt_str = fmt::format("[{{0:{0}}}]: ", max_width);
            for (auto i = 0u; i < value.size(); i++) {
                os << get_indent(indent) << fmt::format(fmt_str.data(), i);
                render(std::move(value[i]), os, new_indent);
            }
        }
    } else if constexpr (std::is_same<hgdb::BreakPoint, T>::value) {
        os << "- id: " << value.id << std::endl;
        os << get_indent(indent) << "- filename: " << value.filename << std::endl;
        os << get_indent(indent) << "- line: " << value.line_num << std::endl;
        if (value.instance_id)
            os << get_indent(indent) << "- instance: " << *value.instance_id << std::endl;
        if (value.column_num)
            os << get_indent(indent) << "- column: " << value.column_num << std::endl;
        if (!value.condition.empty())
            os << get_indent(indent) << "- condition: " << value.condition << std::endl;
    } else if constexpr (std::is_same<hgdb::SymbolTableProvider::ContextVariableInfo, T>::value ||
                         std::is_same<hgdb::SymbolTableProvider::GeneratorVariableInfo, T>::value) {
        auto const &[var, ref] = value;
        os << "- name: " << var.name << std::endl;
        os << get_indent(indent) << "- value: " << ref.value << std::endl;
    } else if constexpr (std::is_same<AssignmentInfo, T>::value) {
        os << "- name: " << value.name << std::endl;
        if (!value.cond.empty()) {
            os << get_indent(indent) << "- condition: " << value.cond << std::endl;
        }
        os << get_indent(indent) << "- var: " << std::endl;
        indent += 2;
        os << get_indent(indent);
        render(std::move(value.bp), os, indent);
    }
}

auto get_instance(cli::Menu &menu, std::unique_ptr<hgdb::SymbolTableProvider> &db) {
    auto sub_menu = std::make_unique<cli::Menu>("instance", "Instance data query");
    sub_menu->Insert("id",
                     [&db](std::ostream &os, uint32_t id) {
                         if (!check_db(db, os)) return;
                         auto instance = db->get_instance_name(id);
                         if (instance) {
                             render(*instance, os);
                         }
                     },
                     "Search instance by ID", {"id"});
    sub_menu->Insert("bp-id",
                     [&db](std::ostream &os, uint32_t id) {
                         if (!check_db(db, os)) return;
                         auto instance = db->get_instance_name_from_bp(id);
                         if (instance) {
                             render(*instance, os);
                         }
                     },
                     "Search instance by breakpoint ID", {"bp-id"});
    sub_menu->Insert(
        "list",
        [&db](std::ostream &os) {
            if (!check_db(db, os)) return;
            auto instances = db->get_instance_names();
            render<decltype(instances), std::string>(std::move(instances), os);
        },
        "List all filenames");
    return std::move(sub_menu);
}

auto get_breakpoint(cli::Menu &menu, std::unique_ptr<hgdb::SymbolTableProvider> &db) {
    auto sub_menu = std::make_unique<cli::Menu>("breakpoint", "Breakpoint data query");
    sub_menu->Insert("id",
                     [&db](std::ostream &os, uint32_t id) {
                         if (!check_db(db, os)) return;
                         auto bp = db->get_breakpoint(id);
                         if (bp) {
                             render(std::move(*bp), os);
                         }
                     },
                     "Search breakpoint by ID", {"id"});
    sub_menu->Insert("where",
                     [&db](std::ostream &os, const std::string &filename) {
                         std::vector<hgdb::BreakPoint> bps;
                         uint32_t line = 0, col = 0;
                         auto tokens = hgdb::util::get_tokens(filename, ":");
                         if (tokens.size() > 1) {
                             auto const t = tokens[1];
                             if (std::all_of(t.begin(), t.end(), ::isdigit)) {
                                 line = std::stoul(t);
                             } else {
                                 ColorScope color;
                                 os << "Invalid line number " << t << std::endl;
                                 return;
                             }
                         }
                         if (tokens.size() > 2) {
                             auto const t = tokens[2];
                             if (std::all_of(t.begin(), t.end(), ::isdigit)) {
                                 col = std::stoul(t);
                             } else {
                                 ColorScope color;
                                 os << "Invalid column number " << t << std::endl;
                                 return;
                             }
                         }
                         if (line == 0 && col == 0) {
                             bps = db->get_breakpoints(filename);
                         } else if (col == 0) {
                             bps = db->get_breakpoints(filename, line);
                         } else {
                             bps = db->get_breakpoints(filename, line, col);
                         }
                         render<decltype(bps), hgdb::BreakPoint>(std::move(bps), os);
                     },
                     "Search breakpoint by location", {"filename"});
    return std::move(sub_menu);
}

auto get_context_variable(cli::Menu &menu, std::unique_ptr<hgdb::SymbolTableProvider> &db) {
    auto sub_menu = std::make_unique<cli::Menu>("context", "Context variable query");
    sub_menu->Insert("id",
                     [&db](std::ostream &os, uint32_t id) {
                         auto res = db->get_context_variables(id);
                         render<decltype(res), hgdb::SymbolTableProvider::ContextVariableInfo>(
                             std::move(res), os);
                     },
                     "Show context variable by breakpoint ID", {"id"});

    return std::move(sub_menu);
}

auto get_generator_variable(cli::Menu &menu, std::unique_ptr<hgdb::SymbolTableProvider> &db) {
    auto sub_menu = std::make_unique<cli::Menu>("generator", "Generator variable query");
    sub_menu->Insert("id",
                     [&db](std::ostream &os, uint32_t id) {
                         auto res = db->get_generator_variable(id);
                         render<decltype(res), hgdb::SymbolTableProvider::GeneratorVariableInfo>(
                             std::move(res), os);
                     },
                     "Show context variable by instance ID", {"id"});

    return std::move(sub_menu);
}

auto get_assign_info(cli::Menu &menu, std::unique_ptr<hgdb::SymbolTableProvider> &db) {
    auto sub_menu = std::make_unique<cli::Menu>("assign", "Assignment information query query");
    sub_menu->Insert("get",
                     [&db](std::ostream &os, const std::string &var_name, uint32_t id) {
                         auto res = db->get_assigned_breakpoints(var_name, id);
                         std::vector<AssignmentInfo> result;
                         result.reserve(res.size());
                         for (auto const &[bp_id, name, cond] : res) {
                             auto bp = db->get_breakpoint(bp_id);
                             if (bp) {
                                 result.emplace_back(AssignmentInfo{
                                     .bp = std::move(*bp), .name = name, .cond = cond});
                             }
                         }
                         render<decltype(result), AssignmentInfo>(std::move(result), os);
                     },
                     "Get assignment information based on variable name and breakpoint ID",
                     {"name", "id"});
    return std::move(sub_menu);
}

int main(int argc, char *argv[]) {
    auto program_name = get_program_name(argv);
    auto root_menu = std::make_unique<cli::Menu>("hgdb", "HGDB symbol table tool");
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
    root_menu->Insert(get_breakpoint(*root_menu, db));
    root_menu->Insert(get_context_variable(*root_menu, db));
    root_menu->Insert(get_generator_variable(*root_menu, db));
    root_menu->Insert(get_assign_info(*root_menu, db));

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
