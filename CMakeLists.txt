cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_policy(SET CMP0091 NEW)
project(splv_encoder VERSION 1.0)

# add fsanitize
if(NOT WIN32)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined")
endif()

# set output dir
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# find source files:
file(GLOB_RECURSE splv_encoder_src CONFIGURE_DEPENDS "src/*.cpp" "src/*.c")
add_executable(${PROJECT_NAME} ${splv_encoder_src})

# find packages
find_package(pybind11 REQUIRED)

# add python module
pybind11_add_module(py_splv_encoder ${splv_encoder_src})

if(WIN32)
    set_target_properties(py_splv_encoder PROPERTIES SUFFIX ".pyd")
endif()

# add include directories and libraries
include_directories("src/" ${pybind11_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} pybind11::lto pybind11::embed pybind11::module)