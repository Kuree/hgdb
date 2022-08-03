#ifndef HGDB_JSON_HH
#define HGDB_JSON_HH

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace hgdb::json {

class JSONWriter {
public:
    JSONWriter() = default;
    JSONWriter(const JSONWriter &) = delete;
    JSONWriter &operator=(const JSONWriter &) = delete;

    JSONWriter &begin_obj() {
        s_ << '{';
        return *this;
    }

    JSONWriter &end_obj() {
        remove_comma();
        s_ << '}' << ',';
        return *this;
    }

    JSONWriter &key(const std::string_view name) {
        s_ << '"' << name << "\":";
        return *this;
    }

    template <typename T>
    JSONWriter &value(T value) {
        // if it's an array
        if constexpr (std::is_same_v<T, const char *> || std::is_same_v<T, std::string> ||
                      std::is_same_v<T, std::string_view>) {
            s_ << '"' << escape(value) << '"';
        } else if constexpr (std::is_same_v<T, bool>) {
            s_ << (value ? "true" : "false");
        } else {
            s_ << value;
        }
        s_ << ',';
        return *this;
    }

    JSONWriter &begin_array() {
        s_ << '[';
        return *this;
    }

    JSONWriter &end_array() {
        remove_comma();
        s_ << ']' << ',';
        return *this;
    }

    [[nodiscard]] std::string str() {
        remove_comma();
        s_ << '\n';
        return s_.str();
    }

private:
    std::stringstream s_;

    void remove_comma() {
        s_.seekg(-1, std::ios::end);
        char c;
        s_ >> c;
        if (c == ',') {
            s_.seekp(-1, std::ios::end);
        }
    }

    static std::string escape(std::string_view value) {
        std::stringstream ss;
        for (auto c : value) {
            if (c == '\\') {
                ss << '\\' << '\\';
            } else {
                ss << c;
            }
        }

        return ss.str();
    }
};

class Module;
class VarStmt;
class VarStmt;
class SymbolTable;

class ScopeBase {
public:
    virtual void serialize(JSONWriter &w) const = 0;
    virtual ~ScopeBase() = default;
    [[nodiscard]] virtual std::string_view type() const = 0;

    [[nodiscard]] auto begin() const { return scopes_.begin(); }
    [[nodiscard]] auto end() const { return scopes_.end(); }
    [[nodiscard]] auto rbegin() const { return scopes_.rbegin(); }
    [[nodiscard]] auto rend() const { return scopes_.rend(); }
    [[nodiscard]] auto &operator[](uint64_t index) const { return *scopes_[index]; }

    ScopeBase *parent = nullptr;

    [[nodiscard]] virtual const std::string &get_filename() const = 0;

    friend class SymbolTable;
    template <typename C>
    friend class Scope;

protected:
    std::vector<std::unique_ptr<ScopeBase>> scopes_;
    uint64_t index_ = 0;
};

template <typename C = std::nullptr_t>
class Scope : public ScopeBase {
public:
    Scope() = default;
    explicit Scope(uint32_t line) : line_num(line) {}

    template <typename T, typename... Args>
    T *create_scope(Args &&...args) {
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        return add_scope(std::move(ptr));
    }

    [[nodiscard]] std::string_view type() const override {
        return scopes_.empty() ? "none" : "block";
    }

    void serialize(JSONWriter &w) const override {
        w.begin_obj().key("type").value(type());
        if (!filename.empty()) {
            w.key("filename").value(filename);
        }
        if (line_num) {
            w.key("line").value(*line_num);
            if (column_num != 0) {
                w.key("column").value(column_num);
            }
        }
        if (!condition.empty()) {
            w.key("condition").value(condition);
        }

        bool has_scope = !scopes_.empty();
        if constexpr (std::is_same_v<C, Module>) {
            has_scope = true;
        }

        if (has_scope) {
            w.key("scope").begin_array();
            for (auto const &scope : scopes_) {
                scope->serialize(w);
            }
            w.end_array();
        }

        serialize_(w);

        w.end_obj();
    }

    [[nodiscard]] const std::string &get_filename() const override {
        if (!filename.empty()) {
            return filename;
        }
        if (parent) return parent->get_filename();
        return filename;
    }

    template <typename T>
    [[nodiscard]] bool operator==(const Scope<T> &scope) const {
        // notice that we do not compare the inner scopes
        return filename == scope.filename && line_num == scope.line_num &&
               column_num == scope.column_num && condition == scope.condition;
    }

    std::string filename;
    std::optional<uint32_t> line_num;
    uint32_t column_num = 0;
    std::string condition;

    friend class SymbolTable;

protected:
    virtual void serialize_(JSONWriter &) const {}

private:
    template <typename T>
    T *add_scope(std::unique_ptr<T> ptr) {
        // make sure the scope creation is valid
        static_assert(!std::is_same_v<T, Module> && !std::is_same_v<C, VarStmt>, "Invalid scope");
        auto *res = ptr.get();
        res->parent = this;
        // notice that the symbol table is write only. once it's put in, there is no modification
        res->index_ = scopes_.size();
        scopes_.emplace_back(std::move(ptr));
        return res;
    }
};

struct Variable {
    // for now, we don't support global variable reference
    // need to implement compression later on
    std::string name;
    std::string value;
    bool rtl = false;

    std::optional<uint64_t> id;

    void serialize(JSONWriter &w) const {
        w.begin_obj().key("name").value(name).key("value").value(value).key("rtl").value(rtl);
        if (id) {
            w.key("id").value(std::to_string(*id));
        }
        w.end_obj();
    }

    bool operator==(const Variable &v) const {
        return name == v.name && value == v.value && rtl == v.rtl;
    }

    struct hash_fn {
        std::size_t operator()(const Variable &v) const {
            auto hash = std::hash<std::string>{};
            auto h1 = hash(v.name);
            auto h2 = hash(v.value);
            auto h3 = std::hash<bool>{}(v.rtl);
            return h1 ^ h2 ^ h3;
        }
    };
};

class VarStmt : public Scope<VarStmt> {
public:
    VarStmt(Variable var, uint64_t line, bool is_decl)
        : Scope<VarStmt>(line), var_(std::move(var)), is_decl_(is_decl) {}

    [[nodiscard]] std::string_view type() const override { return is_decl_ ? "decl" : "assign"; }

    [[nodiscard]] bool operator==(const VarStmt &stmt) const {
        auto self_uninitialized = filename.empty() && (!line_num || (*line_num == 0));
        auto stmt_uninitialized =
            stmt.filename.empty() && (!stmt.line_num || (*stmt.line_num == 0));
        if (self_uninitialized || stmt_uninitialized) {
            return var_ == stmt.var_;
        } else {
            return Scope<VarStmt>::operator==(stmt) && var_ == stmt.var_;
        }
    }

    friend class SymbolTable;

protected:
    void serialize_(JSONWriter &w) const override {
        w.key("variable");
        if (var_.index() == 0) {
            std::get<0>(var_).serialize(w);
        } else {
            w.value(std::to_string(std::get<1>(var_)));
        }
    }

private:
    std::variant<Variable, uint64_t> var_;
    bool is_decl_;
};

class Module : public Scope<Module> {
public:
    Module(std::string name, std::function<void(const std::string &)> remove_func)
        : name_(std::move(name)), remove_inst_(std::move(remove_func)) {}

    [[nodiscard]] std::string_view type() const override { return "module"; }
    void add_variable(Variable var) { variables_.emplace_back(std::move(var)); }
    void add_instance(const std::string &name, const Module *m) {
        instances_.emplace_back(std::make_pair(name, m));
        remove_inst_(m->name_);
    }

    friend class SymbolTable;

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
            if (v.index() == 0) {
                std::get<0>(v).serialize(w);
            } else {
                w.value(std::to_string(std::get<1>(v)));
            }
        }
        w.end_array();
    }

private:
    std::string name_;
    std::vector<std::variant<Variable, uint64_t>> variables_;
    std::vector<std::pair<std::string, const Module *>> instances_;

    std::function<void(const std::string &)> remove_inst_;
};

class SymbolTable {
private:
    // not efficient but good enough
    class ScopeVisitor {
    public:
        void visit(ScopeBase *scope) {
            if (auto *m = dynamic_cast<Module *>(scope)) {
                for (auto const &ss : *m) {
                    visit(ss.get());
                }
                handle(m);
            } else if (auto *v = dynamic_cast<VarStmt *>(scope)) {
                handle(v);
            } else if (auto *s = dynamic_cast<Scope<> *>(scope)) {
                for (auto const &ss : *s) {
                    visit(ss.get());
                }
                handle(s);
            }
        }
        virtual void handle(Module *) {}
        virtual void handle(VarStmt *) {}
        virtual void handle(Scope<> *) {}
    };

    class VariableCount : public ScopeVisitor {
    public:
        std::unordered_map<Variable, uint64_t, Variable::hash_fn> count;
        void handle(Module *m) override {
            for (auto const &v : m->variables_) {
                if (v.index() == 0) {
                    count[std::get<0>(v)]++;
                }
            }
        }

        void handle(VarStmt *s) override {
            if (s->var_.index() == 0) {
                count[std::get<0>(s->var_)]++;
            }
        }
    };

    class VarAssign : public ScopeVisitor {
    public:
        explicit VarAssign(const std::unordered_map<Variable, uint64_t, Variable::hash_fn> &id)
            : id_(id) {}

        void handle(Module *m) override {
            for (auto &variable : m->variables_) {
                auto const &v = variable;
                if (v.index() == 0) {
                    auto const &var = std::get<0>(v);
                    if (id_.find(var) != id_.end()) {
                        variable = id_.at(var);
                    }
                }
            }
        }

        void handle(VarStmt *s) override {
            if (s->var_.index() == 0) {
                auto const &var = std::get<0>(s->var_);
                if (id_.find(var) != id_.end()) {
                    s->var_ = id_.at(var);
                }
            }
        }

    private:
        const std::unordered_map<Variable, uint64_t, Variable::hash_fn> &id_;
    };

    class FilenameClear : public ScopeVisitor {
    public:
        void handle(Scope<> *scope) override { FilenameClear::handle(scope, scope->filename); }

        void handle(VarStmt *stmt) override { FilenameClear::handle(stmt, stmt->filename); }

    private:
        static void handle(ScopeBase *scope, std::string &filename) {
            if (filename.empty()) return;
            if (!scope->parent) return;
            // top level scope need to have filename
            if (dynamic_cast<Module *>(scope->parent)) return;
            auto const &ref = scope->parent->get_filename();
            if (ref == filename) {
                // the parent already has this filename, don't need to store it
                filename.clear();
            }
        }
    };

public:
    explicit SymbolTable(std::string framework_name) : framework_name_(std::move(framework_name)) {}

    Module *add_module(const std::string &name) {
        // no error check
        top_names_.emplace(name);
        return modules_
            .emplace_back(std::make_unique<Module>(
                name, [this](const std::string &name) { remove_module_top(name); }))
            .get();
    }

    [[nodiscard]] std::string output() const {
        JSONWriter w;
        w.begin_obj();
        w.key("generator").value(framework_name_);

        if (top_names_.size() == 1) {
            w.key("top").value(*top_names_.begin());
        } else {
            w.key("top").begin_array();
            for (auto const &n : top_names_) w.value(n);
            w.end_array();
        }

        w.key("table").begin_array();
        for (auto const &m : modules_) {
            m->serialize(w);
        }
        w.end_array();

        if (!variables_.empty()) {
            w.key("variables");
            w.begin_array();
            for (auto const &v : variables_) {
                v.serialize(w);
            }
            w.end_array();
        }

        // attributes
        if (!reorder_) {
            w.key("reorder").value(false);
        }

        w.end_obj();
        return w.str();
    }

    template <bool include_current>
    static void walk_up(ScopeBase *scope, const std::function<bool(ScopeBase *)> &terminate) {
        auto *parent = scope->parent;
        if (!parent) return;
        if constexpr (include_current) {
            // NOLINTNEXTLINE
            for (auto child = scope->rbegin(); child != scope->rend(); child++) {
                auto res = terminate(child->get());
                if (res) return;
            }
        }

        for (auto i = scope->index_; i > 0; i--) {
            auto &s = parent->operator[](i - 1);
            auto res = terminate(&s);
            if (res) return;
        }
        walk_up<false>(parent, terminate);
    }

    static bool has_same_var(ScopeBase *scope, VarStmt &stmt) {
        bool match = false;
        auto match_func = [&match, &stmt](ScopeBase *s) {
            if (s->type() == "decl" || s->type() == "assign") {
                auto const &ref = *reinterpret_cast<VarStmt *>(s);
                if (stmt == ref) {
                    match = true;
                    return true;
                }
            }
            return false;
        };
        walk_up<true>(scope, match_func);

        return match;
    }

    // reduce the storage
    void compress() {
        // 1. compress variable referencing
        compress_var();
        // 2. compress filename
        compress_filename();
    }

    // setting attributes
    void disable_reorder() { reorder_ = false; }

private:
    std::string framework_name_;
    std::set<std::string> top_names_;
    std::vector<std::unique_ptr<Module>> modules_;

    std::vector<Variable> variables_;

    // some other attributes
    bool reorder_ = true;

    // automatically keep track of top module names;

    void compress_var() {
        // first pass collect all the variables
        VariableCount v_count;
        for (auto &m : modules_) {
            v_count.visit(m.get());
        }
        // compute the global variable pull
        variables_.reserve(v_count.count.size());
        std::unordered_map<Variable, uint64_t, Variable::hash_fn> ids;
        for (auto [var, c] : v_count.count) {
            if (c > 1) {
                auto id = variables_.size();
                auto &var_id = variables_.emplace_back(var);
                var_id.id = id;
                ids.emplace(var_id, id);
            }
        }

        // second pass to assign
        VarAssign var_assign(ids);
        for (auto &m : modules_) {
            var_assign.visit(m.get());
        }
    }

    void compress_filename() {
        FilenameClear f;
        for (auto &m : modules_) {
            f.visit(m.get());
        }
    }

    void remove_module_top(const std::string &name) {
        if (top_names_.find(name) != top_names_.end()) {
            top_names_.erase(name);
        }
    }
};

}  // namespace hgdb::json

#endif  // HGDB_JSON_HH
