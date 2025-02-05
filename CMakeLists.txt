cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0083 NEW)
project(splv_encoder VERSION 1.0)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# fetch packages
include(FetchContent)

FetchContent_Declare(
    c-blosc
    GIT_REPOSITORY https://github.com/Blosc/c-blosc.git
    GIT_TAG        v1.21.6
)

set(BLOSC_IS_SUBPROJECT ON CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(c-blosc)

if(SPLV_BUILD_PYTHON_BINDINGS)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# set source files
set(splv_src
    "src/splv_brick.cpp"
    "src/splv_encoder.cpp"
    "src/splv_frame.cpp"
    "src/splv_vox_utils.cpp"
    "src/splv_nvdb_utils.cpp"
    "src/splv_utils.cpp"
)

# create static library with common code
add_library(splv_encoder_lib STATIC ${splv_src})

target_include_directories(splv_encoder_lib PUBLIC "include/")
target_link_libraries(splv_encoder_lib PUBLIC blosc_static)

# create CLI executable
if(SPLV_BUILD_CLI)
    add_executable(splv_encoder_cli "src/cli_splv_encoder.cpp")

    target_link_libraries(splv_encoder_cli PRIVATE splv_encoder_lib)
endif() 

# add python module
if(SPLV_BUILD_PYTHON_BINDINGS)
    pybind11_add_module(splv_encoder_py "src/py_splv_encoder.cpp")

    if(WIN32)
        set_target_properties(splv_encoder_py PROPERTIES SUFFIX ".pyd")
    endif()

    target_include_directories(splv_encoder_py PUBLIC "include/" PRIVATE ${pybind11_INCLUDE_DIRS})
    target_link_libraries(splv_encoder_py PRIVATE 
        splv_encoder_lib
        pybind11::lto
        pybind11::module
    )
endif()

# create shared library
if(SPLV_BUILD_SHARED)
    add_library(splv_encoder_shared SHARED ${splv_src})

    target_include_directories(splv_encoder_shared PUBLIC "include/")
    target_link_libraries(splv_encoder_shared PUBLIC blosc_static)
endif()