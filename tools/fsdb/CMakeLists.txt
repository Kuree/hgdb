# only enable it if VERDI_HOME is found
if (DEFINED $ENV{VERDI_HOME})
    # need to find the library to link
    find_package(FSDB REQUIRED)
endif()