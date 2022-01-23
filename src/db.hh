#ifndef HGDB_DB_HH
#define HGDB_DB_HH

#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "symbol.hh"

namespace rapidjson {
template <typename T>
struct UTF8;
template <typename T1, typename T2, typename T3>
class GenericDocument;
template <typename T>
class MemoryPoolAllocator;
class CrtAllocator;
}  // namespace rapidjson

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
        uint32_t breakpoint_id) override;
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) override;
    [[nodiscard]] std::vector<std::string> get_instance_names() override;
    [[nodiscard]] std::vector<std::string> get_filenames() override;
    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name) override;
    std::unordered_map<std::string, int64_t> get_context_static_values(
        uint32_t breakpoint_id) override;
    std::vector<std::string> get_all_array_names() override;
    [[nodiscard]] std::vector<std::tuple<uint32_t, std::string, std::string>>
    get_assigned_breakpoints(const std::string &var_name, uint32_t breakpoint_id) override;

    ~DBSymbolTableProvider() override;

    [[nodiscard]] std::optional<std::string> resolve_scoped_name_breakpoint(
        const std::string &scoped_name, uint64_t breakpoint_id) override;
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_instance(
        const std::string &scoped_name, uint64_t instance_id) override;

    [[nodiscard]] std::vector<uint32_t> execution_bp_orders() override;
    [[nodiscard]] bool use_base_name() const { return use_base_name_; }

private:
    std::unique_ptr<DebugDatabase> db_;
    bool is_closed_ = false;
    std::mutex db_lock_;

    bool use_base_name_ = false;
    // scope table not provided - build from heuristics
    std::vector<uint32_t> build_execution_order_from_bp();

    void compute_use_base_name();
};

// json-based symbol table
class JSONSymbolTableProvider : public SymbolTableProvider {
public:
    explicit JSONSymbolTableProvider(const std::string &filename);
    // take over the DB ownership. normally used for testing
    explicit JSONSymbolTableProvider(std::unique_ptr<JSONSymbolTableProvider> db);
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
        uint32_t breakpoint_id) override;
    [[nodiscard]] std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) override;
    [[nodiscard]] std::vector<std::string> get_instance_names() override;
    [[nodiscard]] std::vector<std::string> get_filenames() override;
    [[nodiscard]] std::vector<std::string> get_annotation_values(const std::string &name) override;
    std::unordered_map<std::string, int64_t> get_context_static_values(
        uint32_t breakpoint_id) override;
    std::vector<std::string> get_all_array_names() override;
    [[nodiscard]] std::vector<std::tuple<uint32_t, std::string, std::string>>
    get_assigned_breakpoints(const std::string &var_name, uint32_t breakpoint_id) override;

    ~JSONSymbolTableProvider() override;

    [[nodiscard]] std::optional<std::string> resolve_scoped_name_breakpoint(
        const std::string &scoped_name, uint64_t breakpoint_id) override;
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_instance(
        const std::string &scoped_name, uint64_t instance_id) override;

    [[nodiscard]] std::vector<uint32_t> execution_bp_orders() override;

    static bool valid_json(const std::istream &stream);
private:
    // why on early do you use typedef
    std::shared_ptr<rapidjson::GenericDocument<
        rapidjson::UTF8<char>, rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>,
        rapidjson::CrtAllocator>>
        document_;

    std::ifstream stream_;
};

}  // namespace hgdb

#endif  // HGDB_DB_HH
