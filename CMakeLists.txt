cmake_minimum_required(VERSION 3.15)
project(FortressFinderBase LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
# cubiomes rng.h defines a global lerp(); C++20 math.h injects std::lerp into
# the global namespace and conflicts with it, so the engine stays on C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# cubiomes.
#
# cubiomes is a nested submodule of this repository:
#
#   FortressFinderBase/cubiomes
#
# A standalone build therefore needs no external source tree.  An embedding
# project that already owns a cubiomes checkout can override:
#
#   -DFORTRESS_CUBIOMES_DIR=<path-to-cubiomes>
# ---------------------------------------------------------------------------
set(FORTRESS_CUBIOMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cubiomes"
    CACHE PATH "Path to the cubiomes checkout")
if(NOT EXISTS "${FORTRESS_CUBIOMES_DIR}/finders.c")
    message(FATAL_ERROR
        "FORTRESS_CUBIOMES_DIR does not look like cubiomes: ${FORTRESS_CUBIOMES_DIR}\n"
        "Run 'git submodule update --init --recursive' in FortressFinderBase, "
        "or pass -DFORTRESS_CUBIOMES_DIR=<path-to-cubiomes>.")
endif()

# fortress_finder.cpp includes "cubiomes/...", so the include root is the
# parent directory of the cubiomes checkout.
get_filename_component(CUBIOMES_INCLUDE_ROOT "${FORTRESS_CUBIOMES_DIR}" DIRECTORY)

set(FORTRESS_CUBIOMES_SOURCES)
file(GLOB _fortress_cubiomes_root_c "${FORTRESS_CUBIOMES_DIR}/*.c")
foreach(_src ${_fortress_cubiomes_root_c})
    get_filename_component(_name "${_src}" NAME)
    if(NOT _name STREQUAL "tests.c")
        list(APPEND FORTRESS_CUBIOMES_SOURCES "${_src}")
    endif()
endforeach()
file(GLOB _fortress_cubiomes_feature_c "${FORTRESS_CUBIOMES_DIR}/features/*.c")
list(APPEND FORTRESS_CUBIOMES_SOURCES ${_fortress_cubiomes_feature_c})

# ---------------------------------------------------------------------------
# Shared static engine.  Both the CLI and the JNI library link this target.
# ---------------------------------------------------------------------------
add_library(fortressfinder-core STATIC
    fortress_finder.cpp
    ${FORTRESS_CUBIOMES_SOURCES}
)

target_include_directories(fortressfinder-core PUBLIC
    "${CUBIOMES_INCLUDE_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(fortressfinder-core PRIVATE /O2 /DNDEBUG)
else()
    target_compile_options(fortressfinder-core PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O3;-DNDEBUG;-ffast-math>
        $<$<COMPILE_LANGUAGE:CXX>:-O3;-DNDEBUG;-ffast-math>
    )
endif()

if(NOT WIN32)
    target_link_libraries(fortressfinder-core PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# Standalone command-line program (self-contained; statically linked on MinGW).
# ---------------------------------------------------------------------------
option(FORTRESSFINDER_BUILD_CLI "Build the standalone fortressfinder-cli" ON)
if(FORTRESSFINDER_BUILD_CLI)
    add_executable(fortressfinder-cli fortress_cli.cpp)
    target_link_libraries(fortressfinder-cli PRIVATE fortressfinder-core)
    set_target_properties(fortressfinder-cli PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(MINGW)
        target_link_options(fortressfinder-cli PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    if(NOT MSVC)
        target_compile_options(fortressfinder-cli PRIVATE -O2 -DNDEBUG)
    endif()
endif()

# ---------------------------------------------------------------------------
# Version consistency checker (development tool).
#
# Compares fortress generation across cubiomes versions over a configurable
# grid of structure regions.  See tools/fortress_version_check.cpp.
# ---------------------------------------------------------------------------
option(FORTRESSFINDER_BUILD_VERSION_CHECK "Build the fortress version consistency checker" ON)
if(FORTRESSFINDER_BUILD_VERSION_CHECK)
    add_executable(fortress-version-check tools/fortress_version_check.cpp)
    target_link_libraries(fortress-version-check PRIVATE fortressfinder-core)
    set_target_properties(fortress-version-check PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(MINGW)
        target_link_options(fortress-version-check PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# JNI library for the FortressFinder Java GUI.
# ---------------------------------------------------------------------------
option(FORTRESSFINDER_BUILD_JNI "Build the fortressFinderLibJ JNI library" ON)
if(FORTRESSFINDER_BUILD_JNI)
    add_subdirectory(jni)
endif()
