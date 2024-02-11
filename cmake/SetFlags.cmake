# check if the compiler support static lib gcc
# we need this because most
CHECK_CXX_COMPILER_FLAG(-static-libgcc COMPILER_STATIC_LIBGCC)
if (${COMPILER_STATIC_LIBGCC})
    set(STATIC_GCC_FLAG "-static-libgcc")
endif ()
CHECK_CXX_COMPILER_FLAG(-static-libstdc++ COMPILER_STATIC_LIBCXX)
if (${COMPILER_STATIC_LIBCXX})
    set(STATIC_CXX_FLAG "-static-libstdc++")
endif ()

# check if the compiler support attributes like [[likely]]
set(CMAKE_REQUIRED_FLAGS "-Wall -Werror")
check_cxx_source_compiles(
        "int main() { if (1) [[likely]] {}}"
        SUPPORT_LIKELY
)
check_cxx_source_compiles(
        "#include <memory>
        std::unique_ptr<int> foo() { return std::move(std::make_unique<int>(0)); }
        int main() { return *foo(); }"
        NO_MOVE_COPY_OPT
)
unset(CMAKE_REQUIRED_FLAGS)
if (NOT SUPPORT_LIKELY)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(EXTRA_FLAGS -Wno-unknown-attributes"")
    else ()
        set(EXTRA_FLAGS "-Wno-attributes")
    endif ()
endif ()
if (NOT NO_MOVE_COPY_OPT)
    set(EXTRA_FLAGS  ${EXTRA_FLAGS} "-Wno-pessimizing-move")
endif ()
