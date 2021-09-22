# only enable it if VERDI_HOME is found
if (DEFINED ENV{VERDI_HOME})
    # need to find the library to link
    find_package(FSDB REQUIRED)
    add_library(fsdb fsdb.cc fsdb.hh)
    target_link_libraries(fsdb verdi::fsdb)
endif()