cmake_minimum_required(VERSION 3.15)

project(fortressFinderLibJ LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
# See the comment in the root CMakeLists.txt: cubiomes rng.h conflicts with
# C++20 std::lerp, so the native side stays on C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# JNI headers.
#
# FindJNI breaks when JAVA_HOME contains spaces on Windows (e.g.
# C:\Program Files\Java\...).  Resolve the JDK manually and let Gradle pass
# -DJAVA_HOME=... with forward slashes.
# ---------------------------------------------------------------------------
if(DEFINED JAVA_HOME)
    set(_JAVA_HOME "${JAVA_HOME}")
elseif(DEFINED ENV{JAVA_HOME})
    set(_JAVA_HOME "$ENV{JAVA_HOME}")
else()
    find_program(_JAVA_EXE java REQUIRED)
    get_filename_component(_JAVA_BIN "${_JAVA_EXE}" DIRECTORY)
    get_filename_component(_JAVA_HOME "${_JAVA_BIN}" DIRECTORY)
endif()

file(TO_CMAKE_PATH "${_JAVA_HOME}" _JAVA_HOME_CMAKE)

set(JNI_INCLUDE_DIRS "${_JAVA_HOME_CMAKE}/include")
if(WIN32)
    list(APPEND JNI_INCLUDE_DIRS "${_JAVA_HOME_CMAKE}/include/win32")
elseif(APPLE)
    list(APPEND JNI_INCLUDE_DIRS "${_JAVA_HOME_CMAKE}/include/darwin")
else()
    list(APPEND JNI_INCLUDE_DIRS "${_JAVA_HOME_CMAKE}/include/linux")
endif()

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FORTRESS_SOURCE_DIR "${PROJECT_ROOT}")

# The JNI header is checked in so FortressFinderBase builds without the Java
# sources.  Whenever FortressFinderBridge.java changes, regenerate it and copy
# it here; the main module's verifyJniContract task enforces the match.
set(JNI_GEN_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/sunnyslopes_fortressfinder_FortressFinderBridge.h")
if(NOT EXISTS "${JNI_GEN_HEADER}")
    message(FATAL_ERROR "Missing JNI header: ${JNI_GEN_HEADER}")
endif()

set(FORTRESS_JNI_SOURCES
    sunnyslopes_fortressfinder_FortressFinderBridge.cpp
    ${JNI_GEN_HEADER}
)

# When configured from the repository root, link the shared static engine.
# When configured standalone (cmake -S jni -B ...), compile the engine and the
# nested cubiomes checkout directly so this directory remains self-contained.
if(TARGET fortressfinder-core)
    add_library(fortressFinderLibJ SHARED ${FORTRESS_JNI_SOURCES})
    target_link_libraries(fortressFinderLibJ PRIVATE fortressfinder-core)
    target_include_directories(fortressFinderLibJ PRIVATE
        ${JNI_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
else()
    set(FORTRESS_CUBIOMES_DIR "${PROJECT_ROOT}/cubiomes"
        CACHE PATH "Path to the cubiomes checkout")
    if(NOT EXISTS "${FORTRESS_CUBIOMES_DIR}/finders.c")
        message(FATAL_ERROR
            "FORTRESS_CUBIOMES_DIR does not look like cubiomes: ${FORTRESS_CUBIOMES_DIR}\n"
            "Run 'git submodule update --init --recursive' in FortressFinderBase.")
    endif()
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

    add_library(fortressFinderLibJ SHARED
        ${FORTRESS_JNI_SOURCES}
        "${PROJECT_ROOT}/fortress_finder.cpp"
        ${FORTRESS_CUBIOMES_SOURCES}
    )
    target_include_directories(fortressFinderLibJ PRIVATE
        ${JNI_INCLUDE_DIRS}
        "${CUBIOMES_INCLUDE_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_ROOT}"
    )
endif()

if(MSVC)
    target_compile_options(fortressFinderLibJ PRIVATE /O2 /DNDEBUG)
else()
    target_compile_options(fortressFinderLibJ PRIVATE -O3 -DNDEBUG -ffast-math)
endif()

if(MINGW)
    target_link_options(fortressFinderLibJ PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

set_target_properties(fortressFinderLibJ PROPERTIES
    PREFIX ""
    OUTPUT_NAME "fortressFinderLibJ"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native"
)

if(NOT MSVC)
    if(NOT CMAKE_STRIP)
        find_program(CMAKE_STRIP strip)
    endif()
    if(CMAKE_STRIP)
        add_custom_command(TARGET fortressFinderLibJ POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:fortressFinderLibJ>
        )
    endif()
endif()
