#ifndef HGDB_DB_HH
#define HGDB_DB_HH

#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "symbol.hh"

namespace hgdb {
/**
 * Debug database class that handles querying about the design
 */
class DBSymbolTableProvider : public SymbolTableProvider {
public:
    explicit DBSymbolTableProvider(const std::string &filename);
    // take over the DB ownership. normally used for testing
    explicit DBSymbolTableProvider(std::unique_ptr<SQLiteDebugDatabase> db);
    void close();

    // helper functions to query the database
    std::vector<BreakPoint> get_breakpoints(const std::string &filename,
                                            uint32_t line_num) override {
        return get_breakpoints(filename, line_num, 0);
    }
    std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                            uint32_t col_num) override;
    std::vector<BreakPoint> get_breakpoints(const std::string &filename) override;
    std::optional<BreakPoint> get_breakpoint(uint32_t breakpoint_id) override;
    std::optional<std::string> get_instance_name(uint32_t id) override;
    std::optional<uint64_t> get_instance_id(const std::string &instance_name) override;
    [[nodiscard]] std::optional<uint64_t> get_instance_id(uint64_t breakpoint_id) override;
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id) override;
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) override;
    [[nodiscard]] std::vector<std::string> get_instance_names() override;
    [[nodiscard]] std::vector<std::string> get_filenames() override;
    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name) override;
    std::vector<std::string> get_all_array_names() override;
    [[nodiscard]] std::vector<std::tuple<uint32_t, std::string, std::string>>
    get_assigned_breakpoints(const std::string &var_name, uint32_t breakpoint_id) override;

    ~DBSymbolTableProvider() override;

    [[nodiscard]] std::vector<uint32_t> execution_bp_orders() override;
    [[nodiscard]] bool use_base_name() const { return use_base_name_; }

    [[nodiscard]] bool bad() const override { return db_ == nullptr; }

    // used for testing and benchmarking. not for normal usage
    void set_context_delay_var(uint32_t breakpoint_id, const std::string &name,
                               const std::string &value);

private:
    std::unique_ptr<SQLiteDebugDatabase> db_;
    bool is_closed_ = false;
    std::mutex db_lock_;

    bool use_base_name_ = false;
    // scope table not provided - build from heuristics
    std::vector<uint32_t> build_execution_order_from_bp();

    void compute_use_base_name();

    // used for testing and debugging, not for production use
    bool debug_mode_ = false;
    // only need one for now
    std::unordered_map<uint32_t, std::pair<std::string, std::string>> debug_context_vars_;
};

namespace db::json {
struct ModuleDef;
struct Instance;
struct VarDef;
}  // namespace db::json

// json-based symbol table
class JSONSymbolTableProvider : public SymbolTableProvider {
public:
    JSONSymbolTableProvider() = default;
    explicit JSONSymbolTableProvider(const std::string &filename);
    // take over the DB ownership. normally used for testing
    explicit JSONSymbolTableProvider(std::unique_ptr<JSONSymbolTableProvider> db);

    // helper functions to query the database
    std::vector<BreakPoint> get_breakpoints(const std::string &filename,
                                            uint32_t line_num) override {
        return get_breakpoints(filename, line_num, 0);
    }
    std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                            uint32_t col_num) override;
    std::vector<BreakPoint> get_breakpoints(const std::string &filename) override;
    std::optional<BreakPoint> get_breakpoint(uint32_t breakpoint_id) override;
    std::optional<std::string> get_instance_name(uint32_t id) override;
    std::optional<uint64_t> get_instance_id(const std::string &instance_name) override;
    [[nodiscard]] std::optional<uint64_t> get_instance_id(uint64_t breakpoint_id) override;
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id) override;
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_delayed_variables(
        uint32_t breakpoint_id) override;
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) override;
    [[nodiscard]] std::vector<std::string> get_instance_names() override;
    [[nodiscard]] std::vector<std::string> get_filenames() override;
    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name) override;
    std::vector<std::string> get_all_array_names() override;
    [[nodiscard]] std::vector<std::tuple<uint32_t, std::string, std::string>>
    get_assigned_breakpoints(const std::string &var_name, uint32_t breakpoint_id) override;

    [[nodiscard]] std::vector<uint32_t> execution_bp_orders() override;

    static bool valid_json(std::istream &stream);

    bool parse(const std::string &db_content);

    [[nodiscard]] inline bool bad() const override { return roots_.empty(); }

private:
    // serialized data structure
    std::vector<std::shared_ptr<db::json::Instance>> roots_;
    std::unordered_map<std::string, std::shared_ptr<db::json::ModuleDef>> module_defs_;
    std::unordered_map<std::string, std::shared_ptr<db::json::VarDef>> var_defs_;
    uint32_t num_bps_ = 0;
    // may change this to per module/instance in the future (backward-compatible)
    std::vector<std::pair<std::string, std::string>> attributes_;

    bool reordering_ = true;

    void parse_db();
};

}  // namespace hgdb

#endif  // HGDB_DB_HH
