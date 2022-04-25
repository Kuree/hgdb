#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "schema.hh"

namespace py = pybind11;

template <typename T>
bool has_type_id(hgdb::SQLiteDebugDatabase &db, uint32_t id) {
    auto ptr = db.get_pointer<T>(id);
    return ptr != nullptr;
}

PYBIND11_MODULE(_hgdb, m) {
    // the debug database class
    py::class_<hgdb::SQLiteDebugDatabase>(m, "DebugDatabase");
    // functions
    m.def("init_debug_db", &hgdb::init_debug_db);
    m.def("store_generator_variable", &hgdb::store_generator_variable);
    m.def("store_context_variable", &hgdb::store_context_variable);
    m.def("store_breakpoint", &hgdb::store_breakpoint);
    m.def("store_variable", &hgdb::store_variable);
    m.def("store_scope", [](hgdb::SQLiteDebugDatabase &db, uint32_t scope_id, py::args args) {
        std::vector<uint32_t> bps;
        bps.reserve(args.size());
        for (auto const &arg : args) {
            auto value = py::cast<uint32_t>(arg);
            bps.emplace_back(value);
        }
        hgdb::store_scope(db, scope_id, bps);
    });
    m.def("store_instance", &hgdb::store_instance);
    m.def("store_annotation", &hgdb::store_annotation);
    m.def("store_assignment",
          py::overload_cast<hgdb::SQLiteDebugDatabase &, const std::string &, const std::string &,
                            uint32_t>(&hgdb::store_assignment));
    m.def("store_assignment",
          py::overload_cast<hgdb::SQLiteDebugDatabase &, const std::string &, const std::string &,
                            uint32_t, const std::string &>(&hgdb::store_assignment));
    m.def("store_assignment",
          py::overload_cast<hgdb::SQLiteDebugDatabase &, const std::string &, const std::string &,
                            uint32_t, const std::string &, uint32_t>(&hgdb::store_assignment));
    // checkers
    m.def("has_instance_id", [](hgdb::SQLiteDebugDatabase &db, uint32_t instance_id) -> bool {
        return has_type_id<hgdb::Instance>(db, instance_id);
    });
    m.def("has_breakpoint_id", [](hgdb::SQLiteDebugDatabase &db, uint32_t breakpoint_id) -> bool {
        return has_type_id<hgdb::BreakPoint>(db, breakpoint_id);
    });
    m.def("has_variable_id", [](hgdb::SQLiteDebugDatabase &db, uint32_t variable_id) -> bool {
        return has_type_id<hgdb::Variable>(db, variable_id);
    });
    // used to speed up insertion speed
    m.def("begin_transaction", [](hgdb::SQLiteDebugDatabase &db) { db.begin_transaction(); });
    m.def("end_transaction", [](hgdb::SQLiteDebugDatabase &db) { db.commit(); });

    // some helper functions for other tools to digest the symbol table
    // especially the local IDE
    m.def("get_filenames", [](hgdb::SQLiteDebugDatabase &db) {
        std::set<std::string> filenames;
        auto breakpoints = db.get_all<hgdb::BreakPoint>();
        for (auto const &bp : breakpoints) {
            filenames.emplace(bp.filename);
        }
        return filenames;
    });
}
