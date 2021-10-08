#ifndef HGDB_SYMBOL_HH
#define HGDB_SYMBOL_HH

#include "schema.hh"

namespace hgdb {

class SymbolTableProvider {
public:
    // helper functions to query the database
    virtual std::vector<BreakPoint> get_breakpoints(const std::string &filename,
                                                    uint32_t line_num) = 0;
    virtual std::vector<BreakPoint> get_breakpoints(const std::string &filename, uint32_t line_num,
                                                    uint32_t col_num) = 0;
    virtual std::vector<BreakPoint> get_breakpoints(const std::string &filename) = 0;
    virtual std::optional<BreakPoint> get_breakpoint(uint32_t breakpoint_id) = 0;
    virtual std::optional<std::string> get_instance_name_from_bp(uint32_t breakpoint_id) = 0;
    virtual std::optional<std::string> get_instance_name(uint32_t instance_id) = 0;
    virtual std::optional<uint64_t> get_instance_id(const std::string &instance_name) = 0;
    [[nodiscard]] virtual std::optional<uint64_t> get_instance_id(uint64_t breakpoint_id) = 0;

    using ContextVariableInfo = std::pair<ContextVariable, Variable>;
    [[nodiscard]] virtual std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id) = 0;
    [[nodiscard]] virtual std::vector<ContextVariableInfo> get_context_variables(
        uint32_t breakpoint_id, bool resolve_hierarchy_value) = 0;

    using GeneratorVariableInfo = std::pair<GeneratorVariable, Variable>;
    [[nodiscard]] virtual std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id, bool resolve_hierarchy_value) = 0;
    [[nodiscard]] virtual std::vector<GeneratorVariableInfo> get_generator_variable(
        uint32_t instance_id) = 0;
    [[nodiscard]] virtual std::vector<std::string> get_instance_names() = 0;
    [[nodiscard]] virtual std::vector<std::string> get_annotation_values(
        const std::string &name) = 0;
    virtual std::unordered_map<std::string, int64_t> get_context_static_values(
        uint32_t breakpoint_id) = 0;
    virtual std::vector<std::string> get_all_array_names() = 0;

    virtual ~SymbolTableProvider() = default;

    // resolve filename or symbol names
    virtual void set_src_mapping(const std::map<std::string, std::string> &mapping) = 0;
    [[nodiscard]] virtual std::string resolve_filename_to_db(const std::string &filename) = 0;
    [[nodiscard]] virtual std::string resolve_filename_to_client(const std::string &filename) = 0;
    [[nodiscard]] virtual std::optional<std::string> resolve_scoped_name_breakpoint(
        const std::string &scoped_name, uint64_t breakpoint_id) = 0;
    [[nodiscard]] virtual std::optional<std::string> resolve_scoped_name_instance(
        const std::string &scoped_name, uint64_t instance_id) = 0;

    // accessors
    [[nodiscard]] virtual const std::vector<uint32_t> &execution_bp_orders() = 0;
};

// based on the schema we make different symbol table
std::unique_ptr<SymbolTableProvider> create_symbol_table(const std::string &filename);

}  // namespace hgdb

#endif  // HGDB_SYMBOL_HH
