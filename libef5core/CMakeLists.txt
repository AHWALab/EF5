cmake_minimum_required(VERSION 3.16)

project(ef5core
    VERSION 2.0.0
    DESCRIPTION "EF5 Hydrological Modeling Core Library"
    LANGUAGES CXX
)

# ============================================================================
# Build Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Options
option(EF5_USE_OPENMP "Enable OpenMP parallelization" ON)
option(EF5_BUILD_TESTS "Build unit tests" OFF)
option(EF5_BUILD_EXAMPLES "Build example programs" OFF)
option(EF5_BUILD_PYTHON "Build Python bindings" ON)

# ============================================================================
# Find Dependencies
# ============================================================================

# OpenMP (optional but recommended)
if(EF5_USE_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        message(STATUS "OpenMP found: ${OpenMP_CXX_VERSION}")
    else()
        message(WARNING "OpenMP not found - building without parallel support")
    endif()
endif()

# ============================================================================
# Library Target
# ============================================================================

# Source files
set(EF5CORE_SOURCES
    src/ef5core.cpp
    src/crest.cpp
    src/kinematic.cpp
    src/sac.cpp
    src/snow17.cpp
    src/hymod.cpp
    src/linear_route.cpp
)

# Header files (for IDEs)
set(EF5CORE_HEADERS
    include/ef5core/ef5core.hpp
    include/ef5core/types.hpp
    include/ef5core/crest.hpp
    include/ef5core/kinematic.hpp
    include/ef5core/sac.hpp
    include/ef5core/snow17.hpp
    include/ef5core/hymod.hpp
    include/ef5core/linear_route.hpp
)

# Create the library
add_library(ef5core ${EF5CORE_SOURCES} ${EF5CORE_HEADERS})

# Include directories
target_include_directories(ef5core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Compiler options
target_compile_options(ef5core PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Optimization flags for Release builds
target_compile_options(ef5core PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<CONFIG:Release>>:-O3 -march=native -ffast-math>
    $<$<AND:$<CXX_COMPILER_ID:Clang>,$<CONFIG:Release>>:-O3 -march=native -ffast-math>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
)

# Link OpenMP if available
if(OpenMP_CXX_FOUND)
    target_link_libraries(ef5core PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(ef5core PUBLIC EF5_HAVE_OPENMP)
endif()

# ============================================================================
# Python Bindings
# ============================================================================

if(EF5_BUILD_PYTHON)
    # Find Python
    find_package(Python3 COMPONENTS Interpreter Development NumPy REQUIRED)
    message(STATUS "Python found: ${Python3_EXECUTABLE} (${Python3_VERSION})")
    message(STATUS "NumPy found: ${Python3_NumPy_INCLUDE_DIRS}")
    
    # Add pybind11 subdirectory
    add_subdirectory(extern/pybind11)
    
    # Create Python module
    pybind11_add_module(_ef5core python/bindings.cpp)
    
    target_link_libraries(_ef5core PRIVATE ef5core)
    
    target_include_directories(_ef5core PRIVATE
        ${Python3_NumPy_INCLUDE_DIRS}
    )
    
    # Set output directory for easier testing
    set_target_properties(_ef5core PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ef5py"
    )
    
    # Create __init__.py for the ef5py package
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ef5py/__init__.py"
"\"\"\"
EF5 Python Package - Hydrological Modeling

This package provides Python bindings to the EF5 computational core.
\"\"\"

from ._ef5core import *

__version__ = get_version() if 'get_version' in dir() else '2.0.0'
")
endif()

# ============================================================================
# Installation
# ============================================================================

include(GNUInstallDirs)

# Install the library
install(TARGETS ef5core
    EXPORT ef5coreTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers
install(DIRECTORY include/ef5core
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Export targets
install(EXPORT ef5coreTargets
    FILE ef5coreTargets.cmake
    NAMESPACE ef5::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ef5core
)

# Create package config
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ef5coreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Write config file directly (avoid .in file that gets gitignored)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ef5coreConfig.cmake"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/ef5coreTargets.cmake\")
")

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ef5coreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ef5coreConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ef5core
)

# ============================================================================
# Tests
# ============================================================================

if(EF5_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Examples
# ============================================================================

if(EF5_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "EF5 Core Library Configuration:")
message(STATUS "  Version:    ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  OpenMP:     ${EF5_USE_OPENMP} (found: ${OpenMP_CXX_FOUND})")
message(STATUS "  Python:     ${EF5_BUILD_PYTHON}")
message(STATUS "  Tests:      ${EF5_BUILD_TESTS}")
message(STATUS "  Examples:   ${EF5_BUILD_EXAMPLES}")
message(STATUS "")
