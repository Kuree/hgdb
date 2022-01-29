#include <filesystem>

#include "../src/db.hh"
#include "argparse/argparse.hpp"
#include "fmt/format.h"

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define VERSION_STR STRINGIFY(VERSION_NUMBER)

std::optional<std::string> parse_main_arg(const std::vector<std::string> &args) {
    argparse::ArgumentParser parser(fmt::format("HGDB JSON Tool v{0}", VERSION_STR));
    parser.add_argument("file").help("input JSON filename");
    std::string path;
    try {
        parser.parse_args(args);
        path = parser.get<std::string>("file");
    } catch (const std::runtime_error &error) {
        std::cerr << error.what() << std::endl;
        return std::nullopt;
    }
    if (!std::filesystem::exists(path)) {
        std::cerr << path << " does not exist" << std::endl;
        return std::nullopt;
    }
    return path;
}

int main(int argc, char *argv[]) {
    std::vector<std::string> args(argc);
    for (int i = 0; i < argc; i++) args[i] = argv[i];

    auto filename = parse_main_arg(args);
    if (!filename) return EXIT_FAILURE;

    auto db = hgdb::JSONSymbolTableProvider(*filename);
    if (db.bad()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
