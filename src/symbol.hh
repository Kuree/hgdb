#ifndef HGDB_SYMBOL_HH
#define HGDB_SYMBOL_HH

#include <functional>
#include <limits>
#include <map>

#include "schema.hh"

namespace hgdb {

class SymbolTableProvider {
public:
    enum class VariableType : uint32_t { normal = 0, delay = 1 };
    // helper functions to query the database
    virtual std::vector<BreakPoint> get_breakpoints(const std::string &filename,
                                                    uint32_t line_num) = 0;
    virtual std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                                    uint32_t col_num) = 0;
    virtual std::vector<BreakPoint> get_breakpoints(const std::string &filename) = 0;
    virtual std::optional<BreakPoint> get_breakpoint(uint32_t breakpoint_id) = 0;
    std::optional<std::string> get_instance_name_from_bp(uint32_t breakpoint_id);
    virtual std::optional<std::string> get_instance_name(uint32_t instance_id) = 0;
    virtual std::optional<uint64_t> get_instance_id(const std::string &instance_name) = 0;
    [[nodiscard]] virtual std::optional<uint64_t> get_instance_id(uint64_t breakpoint_id) = 0;
    virtual std::vector<std::string> get_filenames() = 0;

    using ContextVariableInfo = std::pair<ContextVariable, Variable>;
    [[nodiscard]] virtual std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id) = 0;
    [[nodiscard]] virtual std::vector<ContextVariableInfo> get_context_delayed_variables(
        uint32_t breakpoint_id);

    using GeneratorVariableInfo = std::pair<GeneratorVariable, Variable>;
    [[nodiscard]] virtual std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) = 0;
    [[nodiscard]] virtual std::vector<std::string> get_instance_names() = 0;
    [[nodiscard]] virtual std::vector<std::string> get_annotation_values(
        const std::string &name) = 0;
    std::unordered_map<std::string, int64_t> get_context_static_values(uint32_t breakpoint_id);
    virtual std::vector<std::string> get_all_array_names() = 0;

    virtual ~SymbolTableProvider() = default;

    // resolve filename or symbol names
    void set_src_mapping(const std::map<std::string, std::string> &mapping);
    [[nodiscard]] std::string resolve_filename_to_db(const std::string &filename);
    [[nodiscard]] std::string resolve_filename_to_client(const std::string &filename);
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_breakpoint(
        const std::string &scoped_name, uint64_t breakpoint_id);
    [[nodiscard]] std::optional<std::string> resolve_scoped_name_instance(
        const std::string &scoped_name, uint64_t instance_id);
    // tuple info: breakpoint_id, var_name, condition (can be empty)
    [[nodiscard]] virtual std::vector<std::tuple<uint32_t, std::string, std::string>>
    get_assigned_breakpoints(const std::string &var_name, uint32_t breakpoint_id) = 0;
    // set method for getting symbol tables. currently not accessible to network-based symbol
    // table
    void set_get_symbol_value(std::function<std::optional<int64_t>(const std::string &)> func);

    // accessors
    [[nodiscard]] virtual std::vector<uint32_t> execution_bp_orders() = 0;

    [[nodiscard]] virtual bool bad() const = 0;

protected:
    [[nodiscard]] bool has_src_remap() const { return !src_remap_.empty(); }
    std::optional<std::function<std::optional<int64_t>(const std::string &)>> get_symbol_value_;

private:
    // we handle the source remap here
    std::map<std::string, std::string> src_remap_;
    // we also allocate temp ID here. notice that we never reuse ID, even for variable
    // with the same name
    // any id larger than 2^31 are reserved for system
    uint32_t id_allocator_ = std::numeric_limits<uint32_t>::max();
};

// based on the schema we make different symbol table
std::unique_ptr<SymbolTableProvider> create_symbol_table(const std::string &filename);

}  // namespace hgdb

#endif  // HGDB_SYMBOL_HH
