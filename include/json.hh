#ifndef HGDB_JSON_HH
#define HGDB_JSON_HH

#include <memory>
#include <sstream>
#include <stack>
#include <string_view>
#include <vector>

namespace hgdb::json {

class JSONWriter {
public:
    JSONWriter &begin_obj() {
        s_ << '{';
        empty_.push(true);
        is_obj_.push(true);
        return *this;
    }

    JSONWriter &end_obj() {
        s_ << '}';
        empty_.pop();
        is_obj_.pop();
        return *this;
    }

    JSONWriter &key(const std::string_view name) {
        // we don't do any error checking
        if (is_obj_.top()) {
            // if it's not empty
            write_comma();
        }
        s_ << '"' << name << "\":";
        return *this;
    }

    template <typename T>
    JSONWriter &value(T value) {
        // if it's an array
        if (!is_obj_.top()) {
            write_comma();
        }
        if constexpr (std::is_same_v<T, const char *> || std::is_same_v<T, std::string> ||
                      std::is_same_v<T, std::string_view>) {
            s_ << '"' << value << '"';
        } else if constexpr (std::is_same_v<T, bool>) {
            s_ << (value ? "true" : "false");
        } else {
            s_ << value;
        }
    }

    JSONWriter &begin_array() {
        s_ << '[';
        empty_.push(true);
        is_obj_.push(false);
        return *this;
    }

    JSONWriter &end_array() {
        empty_.pop();
        is_obj_.pop();
        s_ << ']';
    }

    [[nodiscard]] std::string str() const { return s_.str(); }

private:
    std::stringstream s_;
    // keep track of number of elements in the scope
    std::stack<bool> empty_;
    std::stack<bool> is_obj_;

    void write_comma() {
        if (!empty_.top()) {
            s_ << ',';
        } else {
            empty_.pop();
            empty_.push(false);
        }
    }
};

class Module;
class Scope {
public:
    Scope() = default;
    template <typename T, typename... Args>
    T *create_scope(Args &&...args) {
        static_assert(!std::is_same_v<T, Module>);
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        auto *res = ptr.get();
        scopes_.emplace_back(std::move(ptr));
        return res;
    }

    [[nodiscard]] virtual std::string_view type() const {
        return scopes_.empty() ? "none" : "block";
    }

    virtual void serialize(JSONWriter &w) const {
        w.begin_obj().key("type").value(type());
        if (!filename_.empty()) {
            w.key("filename").value(filename_);
        }
        if (line_num != 0) {
            w.key("line").value(line_num);
            if (column_num != 0) {
                w.key("column").value(column_num);
            }
        }
        w.key("scope").begin_array();
        for (auto const &scope : scopes_) {
            scope->serialize(w);
        }
        w.end_array();

        serialize_(w);

        w.end_obj();
    }

    [[nodiscard]] auto begin() const { return scopes_.begin(); }
    [[nodiscard]] auto end() const { return scopes_.end(); }

    ~Scope() = default;

    std::string filename_;
    uint32_t line_num = 0;
    uint32_t column_num = 0;

protected:
    virtual void serialize_(JSONWriter &) const {}

private:
    std::vector<std::unique_ptr<Scope>> scopes_;
};

struct Variable {
    // for now, we don't support global variable reference
    // need to implement compression later on
    std::string name;
    std::string value;
    bool rtl = false;

    void serialize(JSONWriter &w) const {
        w.begin_obj()
            .key("name")
            .value(name)
            .key("value")
            .value(value)
            .key("rtl")
            .value(rtl)
            .end_obj();
    }
};

class VarStmt : Scope {
public:
    VarStmt(Variable var, bool is_decl) : var_(std::move(var)), is_decl_(is_decl) {}

    [[nodiscard]] std::string_view type() const override { return is_decl_ ? "decl" : "assign"; }

protected:
    void serialize_(JSONWriter &w) const override {
        w.key("variable");
        var_.serialize(w);
    }

private:
    Variable var_;
    bool is_decl_;
};

class Module : public Scope {
public:
    explicit Module(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] std::string_view type() const override { return "module"; }
    void add_variable(Variable &&var) { variables_.emplace_back(var); }
    void add_instance(const std::string &name, const Module *m) {
        instances_.emplace_back(std::make_pair(name, m));
    }

protected:
    void serialize_(JSONWriter &w) const override {
        w.key("name").value(name_);
        w.key("instances").begin_array();
        for (auto const &[name, m] : instances_) {
            w.begin_obj().key("name").value(name).key("module").value(m->name_).end_obj();
        }
        w.end_array();
        w.key("variables").begin_array();
        for (auto const &v : variables_) {
            v.serialize(w);
        }
        w.end_array();
    }

private:
    std::string name_;
    std::vector<Variable> variables_;
    std::vector<std::pair<std::string, const Module *>> instances_;
};

class SymbolTable {
public:
    SymbolTable(std::string framework_name, std::string top_name)
        : framework_name_(std::move(framework_name)), top_name_(std::move(top_name)) {}
    Module *add_module(const std::string &name) {
        return modules_.emplace_back(std::make_unique<Module>(name)).get();
    }

    [[nodiscard]] std::string output() const {
        JSONWriter w;
        w.begin_obj();
        w.key("generator").value(framework_name_).key("top").value(top_name_);

        for (auto const &m : modules_) {
            m->serialize(w);
        }

        w.end_obj();
        return w.str();
    }

private:
    std::string framework_name_;
    std::string top_name_;
    std::vector<std::unique_ptr<Module>> modules_;
};

}  // namespace hgdb::json

#endif  // HGDB_JSON_HH
