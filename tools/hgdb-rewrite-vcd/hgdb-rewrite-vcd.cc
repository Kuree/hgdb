#include <iostream>
#include <memory>
#include <stack>
#include <unordered_map>

#include "../vcd/vcd.hh"
#include "fmt/format.h"

void print_help(const std::string &program_name) {
    std::cerr << "Usage: " << program_name << " <original.vcd> <new_vcd>" << std::endl;
}

class VCDReWriter {
public:
    VCDReWriter(const std::string &filename, const std::string &new_filename) : parser_(filename) {
        stream_ = std::ofstream(new_filename);

        // need to set up all the callback functions
        parser_.set_on_meta_info([this](const hgdb::vcd::VCDMetaInfo &info) {
            // we don't touch serialize information
            serialize(info);
        });

        parser_.set_on_enter_scope(
            [this](const hgdb::vcd::VCDScopeDef &def) { on_scope_def(def); });

        parser_.set_on_var_def([this](const hgdb::vcd::VCDVarDef &def) { on_var_def(def); });

        parser_.set_exit_scope([this]() { scopes_stack_.pop(); });

        parser_.set_value_change([this](const hgdb::vcd::VCDValue &value) { serialize(value); });

        parser_.set_on_time_change([this](uint64_t timestamp) { serialize(timestamp); });

        parser_.set_on_definition_finished([this]() { remap_definition(); });
    }

    bool convert() { return parser_.parse(); }
    [[nodiscard]] const std::string &error_msg() const { return parser_.error_message(); }

private:
    hgdb::vcd::VCDParser parser_;
    std::ofstream stream_;

    struct Scope {
        hgdb::vcd::VCDScopeDef def;
        std::vector<Scope *> scopes;
        std::vector<hgdb::vcd::VCDVarDef *> vars;
        Scope *parent = nullptr;
    };

    std::vector<std::unique_ptr<Scope>> scopes_;
    std::vector<std::unique_ptr<hgdb::vcd::VCDVarDef>> vars_;
    std::unordered_set<std::string> identifiers_;
    std::stack<Scope *> scopes_stack_;
    Scope *root_ = nullptr;

    constexpr static auto indent_ = "  ";
    constexpr static auto end_ = "$end";

    void on_scope_def(const hgdb::vcd::VCDScopeDef &def) {
        auto scope = std::make_unique<Scope>();
        scope->def = def;
        if (!root_) {
            root_ = scope.get();
        } else {
            auto *parent = scopes_stack_.top();
            parent->scopes.emplace_back(scope.get());
            scope->parent = parent;
        }
        scopes_stack_.push(scope.get());
        scopes_.emplace_back(std::move(scope));
    }

    void on_var_def(const hgdb::vcd::VCDVarDef &def) {
        auto var = std::make_unique<hgdb::vcd::VCDVarDef>(def);
        identifiers_.emplace(var->identifier);
        // put it to the parent
        scopes_stack_.top()->vars.emplace_back(var.get());
        vars_.emplace_back(std::move(var));
    }

    void remap_definition() {}

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

    void serialize(uint64_t timestamp) { stream_ << '#' << timestamp << std::endl; }

    void serialize(const hgdb::vcd::VCDValue &info) {
        // we directly dump it since we only modify the value definition
        if (!info.is_event) {
            stream_ << 'b';
        }
        stream_ << info.value;
        if (!info.is_event) {
            stream_ << ' ';
        }
        stream_ << info.identifier << std::endl;
    }

    // remapping logic
    static std::string full_name(Scope *scope) {
        std::string result = scope->def.name;
        auto const *s = scope->parent;
        while (s) {
            result = fmt::format("{0}.{1}", s->def.name, result);
            s = s->parent;
        }
        return result;
    }
};

int main(int argc, char *argv[]) {
    if (argc != 3 || std::string(argv[1]) == std::string(argv[2])) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    std::string filename = argv[1];
    std::string new_filename = argv[2];

    VCDReWriter rewriter(filename, new_filename);

    if (!rewriter.convert()) {
        std::cerr << "ERROR: " << rewriter.error_msg() << std::endl;
    }
}