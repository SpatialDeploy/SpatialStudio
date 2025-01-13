cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_policy(SET CMP0091 NEW)
project(splv_encoder VERSION 1.0)

# add custom cmake module location files
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# find packages
find_package(pybind11 REQUIRED)
find_package(Blosc REQUIRED)

# set output dir
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# make sure that all configurations use the same output directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

# add CLI executable:
add_executable(${PROJECT_NAME} 
    "src/cli_splv_encoder.cpp" 
    "src/splv_brick.cpp" 
    "src/splv_encoder.cpp" 
    "src/splv_frame.cpp" 
    "src/splv_vox_utils.cpp" 
    "src/splv_nvdb_utils.cpp"
)

# add python module
#pybind11_add_module(py_splv_encoder
#    "src/py_splv_encoder.cpp" 
#    "src/splv_brick.cpp" 
#    "src/splv_encoder.cpp" 
#    "src/splv_frame.cpp" 
#    "src/splv_vox_utils.cpp" 
#    "src/splv_nvdb_utils.cpp"
#)

if(WIN32)
#    set_target_properties(py_splv_encoder PROPERTIES SUFFIX ".pyd")
endif()

# add include directories and libraries
include_directories("include/" ${pybind11_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE Blosc::blosc)
#target_link_libraries(py_splv_encoder PRIVATE Blosc::blosc pybind11::lto pybind11::embed pybind11::module)
