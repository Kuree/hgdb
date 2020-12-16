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
    uint32_t column_num = 0;
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
     * Instances the breakpoint belongs to.
     */
    uint32_t instance_id;
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
     * The debugger will prefix testbench related hierarchy name during tests
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
     * Unique ID for each scope.
     */
    uint32_t id;
    /**
     * Space or comma separated list of breakpoint ids, e.g. 0, 1, 2, 3 or 0 1 2 3
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
     * If the variable represents a RTL signal, it is the full hierarchy name,
     * otherwise it is the string value
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
    uint32_t breakpoint_id;
    /**
     * Variable ID associated with the context variable
     */
    uint32_t variable_id;
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
    uint32_t instance_id;
    /**
     * Variable ID associated with the generator variable
     */
    uint32_t variable_id;
};

auto inline init_debug_db(const std::string &filename) {
    using namespace sqlite_orm;
    auto storage = make_storage(
        filename,
        make_table("breakpoint", make_column("id", &BreakPoint::id, primary_key()),
                   make_column("filename", &BreakPoint::filename),
                   make_column("line_num", &BreakPoint::line_num),
                   make_column("column_num", &BreakPoint::column_num),
                   make_column("condition", &BreakPoint::condition),
                   make_column("instance_id", &BreakPoint::instance_id),
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
                   foreign_key(&GeneratorVariable::variable_id).references(&Variable::id)));
    return storage;
}

// type aliasing
using DebugDataBase = decltype(init_debug_db);

}  // namespace hgdb

#endif  // HGDB_SCHEMA_HH
