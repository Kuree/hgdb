#ifndef HGDB_DB_HH
#define HGDB_DB_HH

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
    explicit DBSymbolTableProvider(std::unique_ptr<DebugDatabase> db);
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
    std::optional<std::string> get_instance_name_from_bp(uint32_t breakpoint_id) override;
    std::optional<std::string> get_instance_name(uint32_t id) override;
    std::optional<uint64_t> get_instance_id(const std::string &instance_name) override;
    [[nodiscard]] std::optional<uint64_t> get_instance_id(uint64_t breakpoint_id) override;
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id) override {
        return get_context_variables(breakpoint_id, true);
    }
    [[nodiscard]] std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id, bool resolve_hierarchy_value) override;
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) override {
        return get_generator_variable(instance_id, true);
    }
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id, bool resolve_hierarchy_value) override;
    [[nodiscard]] std::vector<std::string> get_instance_names() override;
    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name) override;
    std::unordered_map<std::string, int64_t> get_context_static_values(
        uint32_t breakpoint_id) override;
    std::vector<std::string> get_all_array_names() override;

    ~DBSymbolTableProvider() override;

    // resolve filename or symbol names
    void set_src_mapping(const std::map<std::string, std::string> &mapping) override;
    [[nodiscard]] std::string resolve_filename_to_db(const std::string &filename) override;
    [[nodiscard]] std::string resolve_filename_to_client(const std::string &filename) override;
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_breakpoint(
        const std::string &scoped_name, uint64_t breakpoint_id) override;
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_instance(
        const std::string &scoped_name, uint64_t instance_id) override;

    // accessors
    [[nodiscard]] const std::vector<uint32_t> &execution_bp_orders() const override {
        return execution_bp_orders_;
    }
    [[nodiscard]] bool use_base_name() const { return use_base_name_; }

private:
    std::unique_ptr<DebugDatabase> db_;
    bool is_closed_ = false;
    std::mutex db_lock_;

    bool use_base_name_ = false;

    // we compute the execution order as we initialize the client, which is defined by the scope
    std::vector<uint32_t> execution_bp_orders_;

    // we handle the source remap here
    std::map<std::string, std::string> src_remap_;

    void setup_execution_order();
    // scope table not provided - build from heuristics
    void build_execution_order_from_bp();

    static std::string resolve(const std::string &src_path, const std::string &dst_path,
                               const std::string &target);
    [[nodiscard]] bool has_src_remap() const { return !src_remap_.empty(); }

    void compute_use_base_name();
};
}  // namespace hgdb

#endif  // HGDB_DB_HH
