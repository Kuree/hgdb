#include <iostream>

void print_help(const std::string &program_name) {
    std::cerr << "Usage: " << program_name << " <original.vcd> <new_vcd>" << std::endl;
}


int main(int argc, char *argv[]) {
    if (argc != 3 || std::string(argv[1]) == std::string(argv[2])) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    std::string filename = argv[1];
    std::string new_filename = argv[2];
    
}