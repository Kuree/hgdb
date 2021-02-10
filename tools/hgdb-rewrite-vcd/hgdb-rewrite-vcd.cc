#include <fstream>
#include <iostream>
#include <unordered_map>

#include "../vcd/vcd.hh"

void print_help(const std::string &program_name) {
    std::cerr << "Usage: " << program_name << " <original.vcd> <new_vcd>" << std::endl;
}

class VCDReWriter {
public:
    VCDReWriter(const std::string &filename, const std::string &new_filename) : parser_(filename) {
        stream_ = std::ofstream(new_filename);

        // need to set up all the callback functions
    }

    void convert() {
        parser_.parse();
    }

private:
    hgdb::vcd::VCDParser parser_;
    std::ofstream stream_;

    constexpr static auto indent_ = "  ";
    constexpr static auto end_ = "$end";

    void serialize(const hgdb::vcd::VCDMetaInfo &info) {
        static const std::unordered_map<hgdb::vcd::VCDMetaInfo::MetaType, std::string> type_map = {
            {hgdb::vcd::VCDMetaInfo::MetaType::date, "$date"},
            {hgdb::vcd::VCDMetaInfo::MetaType::version, "$version"},
            {hgdb::vcd::VCDMetaInfo::MetaType::timescale, "$timescale"},
            {hgdb::vcd::VCDMetaInfo::MetaType::comment, "$comment"},
        };

        stream_ << type_map.at(info.type) << std::endl;
        stream_ << indent_ << info.value << std::endl;
        stream_ << end_ << std::endl;
    }
};

int main(int argc, char *argv[]) {
    if (argc != 3 || std::string(argv[1]) == std::string(argv[2])) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    std::string filename = argv[1];
    std::string new_filename = argv[2];
}