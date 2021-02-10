#include <iostream>
#include <memory>
#include <stack>
#include <unordered_map>

#include "../vcd/vcd.hh"
#include "fmt/format.h"
#include "schema.hh"

void print_help(const std::string &program_name) {
    std::cerr << "Usage: " << program_name << " <original.vcd> <debug.db> <new_vcd>" << std::endl;
}

class VCDReWriter {
public:
    VCDReWriter(const std::string &filename, const std::string &db_name,
                const std::string &new_filename)
        : parser_(filename) {
        stream_ = std::ofstream(new_filename);
        db_ = std::make_unique<hgdb::DebugDatabase>(hgdb::init_debug_db(filename));
        db_->sync_schema();

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

        parser_.set_on_definition_finished([this]() {
            compute_design_root();
            remap_definition();
        });
    }

    bool convert() { return parser_.parse(); }
    [[nodiscard]] const std::string &error_msg() const { return parser_.error_message(); }

private:
    hgdb::vcd::VCDParser parser_;
    std::ofstream stream_;
    std::unique_ptr<hgdb::DebugDatabase> db_;

    struct Scope {
        hgdb::vcd::VCDScopeDef def;
        std::vector<Scope *> scopes;
        std::vector<hgdb::vcd::VCDVarDef *> vars;
        Scope *parent = nullptr;

        void add_child(Scope *scope) {
            scope->parent = this;
            scopes.emplace_back(scope);
        }
    };

    std::vector<std::unique_ptr<Scope>> scopes_;
    std::vector<std::unique_ptr<hgdb::vcd::VCDVarDef>> vars_;
    std::unordered_set<std::string> identifiers_;
    std::stack<Scope *> scopes_stack_;
    Scope *root_ = nullptr;
    Scope *design_root_ = nullptr;
    bool has_error = false;

    constexpr static auto indent_ = "  ";
    constexpr static auto end_ = "$end";

    void on_scope_def(const hgdb::vcd::VCDScopeDef &def) {
        auto scope = std::make_unique<Scope>();
        scope->def = def;
        if (!root_) {
            root_ = scope.get();
        } else {
            auto *parent = scopes_stack_.top();
            parent->add_child(scope.get());
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

    void compute_design_root() {
        // first we need to figure out the actual mapping between TB and the design top
        auto target_instance = find_longer_generator_instance();
        auto tokens = get_tokens(target_instance, ".");
        // we only interest in the ones after that
        // special case where the design is flat
        if (tokens.size() == 1) {
            // choose whatever one that has the highest
            auto *target_scope = root_->scopes[0];
            for (auto *scope : root_->scopes) {
                if (scope->vars.size() > target_scope->vars.size()) target_scope = scope;
            }
            design_root_ = target_scope;
        } else {
            std::vector<Scope *> candidates{root_};
            for (auto i = 1u; i < tokens.size(); i++) {
                std::vector<Scope *> temp;
                for (auto *scope : candidates) {
                    for (auto *child_scope : scope->scopes) {
                        if (child_scope->def.name == tokens[i]) temp.emplace_back(child_scope);
                    }
                }
                candidates = temp;
            }
            if (candidates.size() != 1) {
                std::cerr << "Unable to map design hierarchy to test bench." << std::endl;
                has_error = true;
                return;
            }
            design_root_ = candidates[0];
            for (auto i = 0u; i < tokens.size(); i++) {
                design_root_ = design_root_->parent;
            }
        }
    }

    void remap_definition() {
        using namespace sqlite_orm;
        if (has_error || !design_root_) return;
        // get all the instances and variable name
        auto vars = db_->select(
            columns(&hgdb::Instance::name, &hgdb::Variable::value, &hgdb::GeneratorVariable::name),
            where(c(&hgdb::Variable::is_rtl) == true &&
                  c(&hgdb::GeneratorVariable::variable_id) == &hgdb::Variable::id &&
                  c(&hgdb::GeneratorVariable::instance_id) == &hgdb::Instance::id));
        for (auto const &[instance_name, rtl_name, var_name] : vars) {
            auto *scope = get_scope(instance_name);
            if (!scope) continue;
            // we need to rearrange the vars depends on the var name
            auto const [parent_scope, var_index] = get_var(scope, rtl_name);
            auto *var = parent_scope->vars[var_index];
            // insert the var into a new scope
            insert_new_var(parent_scope, var, var_name);
            // delete the var
            parent_scope->vars[var_index] = nullptr;
        }
    }

    Scope *get_scope(const std::string &design_name) {
        return get_scope(design_root_, design_name);
    }

    static Scope *get_scope(Scope *parent, const std::string &design_name) {
        auto *scope = parent;
        if (!scope) return nullptr;
        auto tokens = get_tokens(design_name, ".");
        for (auto i = 1u; i < tokens.size(); i++) {
            auto const &name = tokens[i];
            bool found = false;
            for (auto *child_scope : scope->scopes) {
                if (child_scope->def.name == name) {
                    scope = child_scope;
                    found = true;
                    break;
                }
            }
            if (!found) return nullptr;
        }
        return scope;
    }

    static std::pair<Scope *, uint64_t> get_var(Scope *parent, const std::string &var_name) {
        // continue to select until we can't any, this would be the parent scope
        auto tokens = get_tokens(var_name, ".");
        Scope *parent_scope = parent;
        uint64_t index;
        for (index = 0; index < tokens.size(); index++) {
            auto *scope = get_scope(parent_scope, tokens[index]);
            if (!scope)
                break;
            else
                parent_scope = scope;
        }
        // we found the parent scope, now need to locate the var
        // compute the rest of var name
        auto name = fmt::format("{}", fmt::join(tokens.begin() + index, tokens.end(), "."));
        for (uint64_t i = 0; i < parent_scope->vars.size(); i++) {
            if (parent_scope->vars[i]->name == name) {
                return {parent_scope, i};
            }
        }
        return {nullptr, 0};
    }

    void insert_new_var(Scope *parent_scope, hgdb::vcd::VCDVarDef *var, const std::string &name) {
        auto tokens = get_tokens(name, ".");
        // create the scopes if not there yet
        auto *scope = parent_scope;
        for (auto i = 0; i < tokens.size() - 1; i++) {
            bool found = false;
            for (auto *child_scope : scope->scopes) {
                if (child_scope->def.name == tokens[i]) {
                    scope = child_scope;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // create a scope
                auto s = std::make_unique<Scope>();
                scope->add_child(s.get());
                s->def.name = tokens[i];
                // VCD only allows a few scope types
                s->def.type = "module";
                scope = s.get();
                scopes_.emplace_back(std::move(s));
            }
        }
        // need to change the var def name
        var->name = tokens.back();
        scope->vars.emplace_back(var);
    }

    void serialize(const Scope *scope) {
        stream_ << "$scope " << scope->def.type << " " << scope->def.name << " " << end_
                << std::endl;
        // dump all var def
        for (auto const *def : scope->vars) {
            if (def) serialize(def);
        }
        stream_ << std::endl;

        // recursively call serialize
        for (auto const *s : scope->scopes) {
            if (s) serialize(s);
        }

        stream_ << std::endl;

        stream_ << "$upscope " << end_ << std::endl;
    }

    void serialize(const hgdb::vcd::VCDVarDef *def) {
        stream_ << "$var " << def->type << " " << def->width << " " << def->identifier << " "
                << def->name << " " << def->slice << " " << end_ << std::endl;
    }

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
        if (has_error) return;
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

    // get all generator names
    std::string find_longer_generator_instance() {
        using namespace sqlite_orm;
        auto result = db_->select(&hgdb::Instance::name,
                                  order_by(length(&hgdb::Instance::name)).desc(), limit(1));
        return result[0];
    }

    static std::vector<std::string> get_tokens(const std::string &line,
                                               const std::string &delimiter) {
        std::vector<std::string> tokens;
        size_t prev = 0, pos;
        std::string token;
        // copied from https://stackoverflow.com/a/7621814
        while ((pos = line.find_first_of(delimiter, prev)) != std::string::npos) {
            if (pos > prev) {
                tokens.emplace_back(line.substr(prev, pos - prev));
            }
            prev = pos + 1;
        }
        if (prev < line.length()) tokens.emplace_back(line.substr(prev, std::string::npos));
        // remove empty ones
        std::vector<std::string> result;
        result.reserve(tokens.size());
        for (auto const &t : tokens)
            if (!t.empty()) result.emplace_back(t);
        return result;
    }
};

std::pair<std::string, std::string> get_args(const std::string &arg1, const std::string &arg2) {
    // use magic four number to detect file types
    std::vector args = {arg1, arg2};
    for (auto i = 0; i < args.size(); i++) {
        std::ifstream stream(args[i]);
        if (stream.bad()) {
            std::cerr << "Unable to open " << arg1 << std::endl;
            return {};
        }
        std::array<char, 7> buff{0};
        stream.readsome(buff.data(), buff.size() - 1);
        buff[6] = '0';
        if (std::string(buff.data()) == "SQLite") {
            // it is the sqlite file
            if (i == 0)
                return {arg2, arg1};
            else
                return {arg1, arg2};
        }
    }
    return {};
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    auto const [filename, db_filename] = get_args(argv[1], argv[2]);
    if (filename.empty() || db_filename.empty()) return EXIT_FAILURE;

    std::string new_filename = argv[3];

    VCDReWriter rewriter(filename, db_filename, new_filename);

    if (!rewriter.convert()) {
        std::cerr << "ERROR: " << rewriter.error_msg() << std::endl;
    }
}