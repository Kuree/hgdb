#include <pybind11/pybind11.h>

#include "schema.hh"

namespace py = pybind11;

PYBIND11_MODULE(_hgdb, m) {
    // the debug database class
    py::class_<hgdb::DebugDatabase>(m, "DebugDatabase");
    // functions
    m.def("init_debug_db", &hgdb::init_debug_db);
    m.def("store_generator_variable", &hgdb::store_generator_variable);
    m.def("store_context_variable", &hgdb::store_context_variable);
    m.def("store_breakpoint", &hgdb::store_breakpoint);
    m.def("store_variable", &hgdb::store_variable);
    m.def("store_scope", [](hgdb::DebugDatabase &db, uint32_t scope_id, py::args args) {
        std::vector<uint32_t> bps;
        bps.reserve(args.size());
        for (auto const &arg: args) {
            auto value = py::cast<uint32_t>(arg);
            bps.emplace_back(value);
        }
        hgdb::store_scope(db, scope_id, bps);
    });
    m.def("store_instance", &hgdb::store_instance);
}
