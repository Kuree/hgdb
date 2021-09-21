set(VERDI_HOME $ENV{VERDI_HOME})
set(FSDB_HOME ${VERDI_HOME}/share/FsdbReader)
set(FSDB_LINUX_LIB ${FSDB_HOME}/Linux64)
set(FSDB_INCLUDE_DIR ${FSDB_HOME})

find_library(LIBNFFR Names nffr HINTS ${FSDB_LINUX_LIB})
find_library(LIBNSYS Names nsys HINTS ${FSDB_LINUX_LIB})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_Args(FSDB DEFAULT_MSG
        LIBNFFR FSDB_INCLUDE_DIR)

mark_as_advanced(FSDB_INCLUDE_DIR LIBNFFR)

add_library(verdi::fsdb INTERFACE IMPORTED)
set_property(TARGET verdi::fsdb PROPERTY INTERFACE_LINK_LIBRARIES ${LIBNFFR} ${LIBNSYS})
set_property(TARGET verdi::fsdb PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${FSDB_INCLUDE_DIR})
