cmake_minimum_required(VERSION 3.19)
project(threepp)

include(CMakeDependentOption)

option(THREEPP_BUILD_EXAMPLES "Build examples" ON)
option(THREEPP_BUILD_TESTS "Build test suite" ON)
option(THREEPP_WITH_AUDIO "Build with Audio" ON)

option(THREEPP_WITH_FBX "Build with FBXLoader (OpenFBX)" OFF)
option(THREEPP_WITH_USD "Build with USDLoader (tinyusdz)" OFF)

option(THREEPP_TREAT_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(THREEPP_WITH_WGPU "Build with WebGPU backend (via wgpu-native)" OFF)
option(THREEPP_WITH_VULKAN "Build with Vulkan backend (requires Vulkan SDK)" OFF)
cmake_dependent_option(THREEPP_WITH_METAL "Build with Metal backend" ON "APPLE" OFF)
cmake_dependent_option(THREEPP_WITH_SLANG "Build Slang shader compiler support" ON "APPLE" OFF)
cmake_dependent_option(THREEPP_WGPU_GLSL_COMPAT
    "Enable GLSL->SPIR-V translation for ShaderMaterial on the Wgpu backend (requires glslang)"
    ON "THREEPP_WITH_WGPU" OFF)

cmake_dependent_option(THREEPP_WITH_INFERENCE "Build with Inference support" OFF "THREEPP_WITH_WGPU OR THREEPP_WITH_VULKAN" OFF)

# Force THREEPP_WITH_GLFW ON when targeting Emscripten
cmake_dependent_option(THREEPP_WITH_GLFW "Build with GLFW frontend" ON "NOT DEFINED EMSCRIPTEN" ON)
# Force THREEPP_USE_EXTERNAL_GLFW OFF if NOT THREEPP_WITH_GLFW
cmake_dependent_option(THREEPP_USE_EXTERNAL_GLFW "Use externally supplied GLFW" OFF "THREEPP_WITH_GLFW" OFF)

cmake_dependent_option(THREEPP_BUILD_EXAMPLE_PROJECTS "Build example projects" ON "THREEPP_BUILD_EXAMPLES" OFF)
option(THREEPP_FETCH_DATA "Fetch threepp_data assets when the local data submodule is unavailable" OFF)


# ==============================================================================
# Global internal configuration
# ==============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if (APPLE AND THREEPP_WITH_METAL)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif ()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


# ==============================================================================
# Dependencies
# ==============================================================================


if (DEFINED EMSCRIPTEN)
    if ("${CMAKE_SYSTEM}" MATCHES "Linux" OR "${CMAKE_SYSTEM}" MATCHES "Darwin-*")
        find_package(OpenGL REQUIRED)
    endif ()
else ()

    if (THREEPP_USE_EXTERNAL_GLFW)
        find_package(glfw3 CONFIG REQUIRED)
    endif ()

endif ()

if (THREEPP_WITH_SLANG)
    find_package(slang CONFIG REQUIRED)
endif ()

include(cmake/ThreeppSlangRuntime.cmake)


# ==============================================================================
# Targets
# ==============================================================================

include(cmake/FetchOptionalDeps.cmake)

add_subdirectory(src)

# Example/test assets live in a separate repo. Prefer the local data/
# submodule so configuring examples/tests does not hit the network by default.
# Set -DTHREEPP_DATA_DIR=<dir> for another checkout, or explicitly enable
# -DTHREEPP_FETCH_DATA=ON to fetch https://github.com/markaren/threepp_data.git.
if (THREEPP_BUILD_EXAMPLES OR THREEPP_BUILD_TESTS)
    set(THREEPP_DATA_DIR "" CACHE PATH "Path to a local threepp_data asset checkout")
    set(THREEPP_LOCAL_DATA_DIR "${PROJECT_SOURCE_DIR}/data")

    if (EXISTS "${THREEPP_LOCAL_DATA_DIR}/textures")
        set(THREEPP_DATA_DIR "${THREEPP_LOCAL_DATA_DIR}" CACHE PATH "Path to a local threepp_data asset checkout" FORCE)
    elseif (THREEPP_DATA_DIR)
        if (NOT IS_ABSOLUTE "${THREEPP_DATA_DIR}")
            get_filename_component(THREEPP_DATA_DIR "${THREEPP_DATA_DIR}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
            set(THREEPP_DATA_DIR "${THREEPP_DATA_DIR}" CACHE PATH "Path to a local threepp_data asset checkout" FORCE)
        endif ()
    elseif (FETCHCONTENT_SOURCE_DIR_THREEPP_DATA)
        set(THREEPP_DATA_DIR "${FETCHCONTENT_SOURCE_DIR_THREEPP_DATA}" CACHE PATH "Path to a local threepp_data asset checkout" FORCE)
    elseif (THREEPP_FETCH_DATA)
        include(FetchContent)
        FetchContent_Declare(
                threepp_data
                GIT_REPOSITORY https://github.com/markaren/threepp_data.git
                GIT_TAG main
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(threepp_data)
        set(THREEPP_DATA_DIR "${threepp_data_SOURCE_DIR}" CACHE PATH "Path to a local threepp_data asset checkout" FORCE)
    else ()
        message(FATAL_ERROR
                "threepp data assets are required when THREEPP_BUILD_EXAMPLES or THREEPP_BUILD_TESTS is ON. "
                "Initialize the data submodule with 'git submodule update --init -- data', "
                "set -DTHREEPP_DATA_DIR=<path>, or opt into online fetching with -DTHREEPP_FETCH_DATA=ON.")
    endif ()

    if (NOT EXISTS "${THREEPP_DATA_DIR}/textures")
        message(FATAL_ERROR "THREEPP_DATA_DIR does not contain the expected textures directory: ${THREEPP_DATA_DIR}")
    endif ()

    message(STATUS "Using threepp data assets from: ${THREEPP_DATA_DIR}")
endif ()

if (THREEPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

if (NOT DEFINED EMSCRIPTEN AND THREEPP_BUILD_TESTS)

    find_package(Catch2 CONFIG REQUIRED)

    enable_testing()
    add_subdirectory(tests)
endif ()


# ==============================================================================
# Exports and remaining installation
# ==============================================================================

configure_package_config_file(cmake/config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/threepp-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/threepp"
        PATH_VARS THREEPP_USE_EXTERNAL_GLFW
        NO_SET_AND_CHECK_MACRO)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/threepp-config-version.cmake"
        VERSION 0.0.0
        COMPATIBILITY AnyNewerVersion)
install(
        FILES
        "${CMAKE_CURRENT_BINARY_DIR}/threepp-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/threepp-config-version.cmake"
        DESTINATION
        "${CMAKE_INSTALL_DATADIR}/threepp")

if (BUILD_SHARED_LIBS AND NOT THREEPP_USE_EXTERNAL_GLFW)
    install(TARGETS glfw EXPORT threepp-targets)
endif ()
if (THREEPP_WITH_USD)
    install(TARGETS tinyusdz_static EXPORT threepp-targets)
    install(DIRECTORY "${tinyusdz_SOURCE_DIR}/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tinyusdz"
            FILES_MATCHING PATTERN "*.hh" PATTERN "*.h" PATTERN "*.inc")
endif ()
install(TARGETS threepp EXPORT threepp-targets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT threepp-targets
        NAMESPACE threepp::
        DESTINATION "${CMAKE_INSTALL_DATADIR}/threepp")
