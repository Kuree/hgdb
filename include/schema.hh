#ifndef HGDB_SCHEMA_HH
#define HGDB_SCHEMA_HH

// assuming sqlite_orm is available in the build system
#include "sqlite_orm/sqlite_orm.h"

namespace hgdb {

/**
 * Breakpoint struct. Each breakpoint shall have a unique ID, which
 * is used as a primary key
 */
struct BreakPoint {
    /**
     * Unique ID for breakpoint
     */
    uint32_t id;
    /**
     * Instances the breakpoint belongs to.
     */
    std::unique_ptr<uint32_t> instance_id;
    /**
     * Path for the source file that generates the corresponding line in
     * absolute path.
     */
    std::string filename;
    /**
     * Line number for the breakpoint. Notice that we use 1 as starting
     * line, which is consistent with most IDE and text editors
     */
    uint32_t line_num;
    /**
     * Column number. Starting from 1. Setting to 0 (default) implies "don't care"
     */
    uint32_t column_num;
    /**
     * Under which condition the breakpoint should be enabled.
     * Notice that unlike software debuggers where there is no default
     * condition associated with newly created breakpoints, hgdb introduces
     * an "innate" condition that is associated with SSA transformation.
     * This condition will always be evaluated and AND with other conditions, if any.
     * If empty, it means it is always valid to break.
     *
     * Variables in the condition are scoped to the instance. For instance, if we have
     * the following RTL code:
     *   module mod;
     *   logic a, b, c;
     *   assign b = c; // <- breakpoint
     *   endmodule
     *
     * And it is only valid if net a is true, we put "a" as condition
     */
    std::string condition;

    /**
     * List of the signals that need to be changed in order to trigger the breakpoint.
     * The difference between trigger and condition is that condition is stateless whereas
     * trigger tracks the signal values and only enables breakpoints whose associated trigger
     * signals have changed.
     *
     * Store as a list of raw signal names (under the scope of instance name) separated by
     * space
     */
    std::string trigger;
};

/**
 * Instance refers to module instantiation in RTL.
 */
struct Instance {
    /**
     * Unique ID for each instance
     */
    uint32_t id;
    /**
     * Full hierarchy name, e.g. cpu.alu.adder
     * the first entry in the hierarchy name shall be the module definition name
     * The debugger will use that top module definition name to search design hierarchy,
     * and then get the proper full path name
     */
    std::string name;
};

/**
 * Scope, which defines a list of breakpoints. This is used to emulate the execution order
 * of the original source language.
 *
 * If scope table is not provided, step-through will be computed in lexical order as indicated
 * in the breakpoint table
 */
struct Scope {
    /**
     * Unique ID for each scope. Scopes with smaller ID values will be evaluated first
     */
    uint32_t id;
    /**
     * Space separated list of breakpoint ids, e.g. 0 1 2 3
     * This is due to the limitation of sqlite database, which doesn't support arrays
     */
    std::string breakpoints;
};

/**
 * Variable can either be RTL signal or simply other values stored as string
 */
struct Variable {
    /**
     * Unique ID for the variable
     */
    uint32_t id;
    /**
     * If the variable represents a RTL signal, it is the full hierarchy name or the name within
     * the scope of its parent module, otherwise it is the string value
     */
    std::string value;
    /**
     * Set to true if the variable represents a RTL signal
     */
    bool is_rtl;
};

/**
 * Context variable when breakpoint is hit
 */
struct ContextVariable {
    /**
     * Variable name in the source language
     */
    std::string name;
    /**
     * Breakpoint ID associated with the context variable
     */
    std::unique_ptr<uint32_t> breakpoint_id;
    /**
     * Variable ID associated with the context variable
     */
    std::unique_ptr<uint32_t> variable_id;
};

/**
 * Generator variable
 */
struct GeneratorVariable {
    /**
     * Generator variable name, e.g. class attribute/field names
     */
    std::string name;
    /**
     * Instance ID associated with the generator variable
     */
    std::unique_ptr<uint32_t> instance_id;
    /**
     * Variable ID associated with the generator variable
     */
    std::unique_ptr<uint32_t> variable_id;
};

/*
 * Annotation on the symbol table. Can be used to store metadata
 * information or pass extra design information to the debugger
 *
 * Some annotation used by the debugger:
 * - clock
 *   * for each unique clock used in your dut, store one entry
 *   * the value should be under the scope of your dut top, e.g.
 *     mod.clk. This follow the same semantics as hierarchy name
 *     in the instance table
 */
struct Annotation {
    /**
     * Annotation name
     */
    std::string name;
    /**
     * Annotation value
     */
    std::string value;
};

auto inline init_debug_db(const std::string &filename) {
    using namespace sqlite_orm;
    auto storage = make_storage(
        filename,
        make_table("breakpoint", make_column("id", &BreakPoint::id, primary_key()),
                   make_column("instance_id", &BreakPoint::instance_id),
                   make_column("filename", &BreakPoint::filename),
                   make_column("line_num", &BreakPoint::line_num),
                   make_column("column_num", &BreakPoint::column_num),
                   make_column("condition", &BreakPoint::condition),
                   make_column("trigger", &BreakPoint::trigger),
                   foreign_key(&BreakPoint::instance_id).references(&Instance::id)),
        make_table("instance", make_column("id", &Instance::id, primary_key()),
                   make_column("name", &Instance::name)),
        make_table("scope", make_column("scope", &Scope::id, primary_key()),
                   make_column("breakpoints", &Scope::breakpoints)),
        make_table("variable", make_column("id", &Variable::id, primary_key()),
                   make_column("value", &Variable::value),
                   make_column("is_rtl", &Variable::is_rtl)),
        make_table("context_variable", make_column("name", &ContextVariable::name),
                   make_column("breakpoint_id", &ContextVariable::breakpoint_id),
                   make_column("variable_id", &ContextVariable::variable_id),
                   foreign_key(&ContextVariable::breakpoint_id).references(&BreakPoint::id),
                   foreign_key(&ContextVariable::variable_id).references(&Variable::id)),
        make_table("generator_variable", make_column("name", &GeneratorVariable::name),
                   make_column("instance_id", &GeneratorVariable::instance_id),
                   make_column("variable_id", &GeneratorVariable::variable_id),
                   foreign_key(&GeneratorVariable::instance_id).references(&Instance::id),
                   foreign_key(&GeneratorVariable::variable_id).references(&Variable::id)),
        make_table("annotation", make_column("name", &Annotation::name),
                   make_column("value", &Annotation::value)));
    storage.sync_schema();
    return storage;
}

// type aliasing
using DebugDatabase = decltype(init_debug_db(""));

// helper functions
inline void store_breakpoint(DebugDatabase &db, uint32_t id, uint32_t instance_id,
                             const std::string &filename, uint32_t line_num,
                             uint32_t column_num = 0, const std::string &condition = "",
                             const std::string &trigger = "") {
    db.replace(BreakPoint{.id = id,
                          .instance_id = std::make_unique<uint32_t>(instance_id),
                          .filename = filename,
                          .line_num = line_num,
                          .column_num = column_num,
                          .condition = condition,
                          .trigger = trigger});
    // NOLINTNEXTLINE
}

inline void store_instance(DebugDatabase &db, uint32_t id, const std::string &name) {
    db.replace(Instance{.id = id, .name = name});
}

inline void store_scope(DebugDatabase &db, uint32_t id, const std::string &breakpoints) {
    db.replace(Scope{.id = id, .breakpoints = breakpoints});
}

inline void store_scope(DebugDatabase &db, uint32_t id, const std::vector<uint32_t> &breakpoints) {
    std::stringstream ss;
    for (auto i = 0u; i < breakpoints.size(); i++) {
        if (i == breakpoints.size() - 1)
            ss << breakpoints[i];
        else
            ss << breakpoints[i] << " ";
    }
    store_scope(db, id, ss.str());
}

template <typename... Ts>
inline void store_scope(DebugDatabase &db, uint32_t id, Ts... ids) {
    store_scope(db, id, std::vector<uint32_t>{ids...});
}

inline void store_variable(DebugDatabase &db, uint32_t id, const std::string &value,
                           bool is_rtl = true) {
    db.replace(Variable{.id = id, .value = value, .is_rtl = is_rtl});
}

inline void store_context_variable(DebugDatabase &db, const std::string &name,
                                   uint32_t breakpoint_id, uint32_t variable_id) {
    db.replace(ContextVariable{.name = name,
                               .breakpoint_id = std::make_unique<uint32_t>(breakpoint_id),
                               .variable_id = std::make_unique<uint32_t>(variable_id)});
    // NOLINTNEXTLINE
}

inline void store_generator_variable(DebugDatabase &db, const std::string &name,
                                     uint32_t instance_id, uint32_t variable_id) {
    db.replace(GeneratorVariable{.name = name,
                                 .instance_id = std::make_unique<uint32_t>(instance_id),
                                 .variable_id = std::make_unique<uint32_t>(variable_id)});
    // NOLINTNEXTLINE
}

inline void store_annotation(DebugDatabase &db, const std::string &name, const std::string &value) {
    db.replace(Annotation{.name = name, .value = value});
}

}  // namespace hgdb

#endif  // HGDB_SCHEMA_HH
