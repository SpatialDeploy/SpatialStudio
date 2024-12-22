cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_policy(SET CMP0091 NEW)
project(splv_encoder VERSION 1.0)

# Add your custom CMake modules directory so that find_package() can locate FindBlosc.cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# add fsanitize
if(NOT WIN32)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined")
endif()

# set output dir
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# add executable:
add_executable(${PROJECT_NAME} "src/brick.cpp" "src/main.cpp" "src/splv_encoder.cpp")

# find packages
find_package(pybind11 REQUIRED)
find_package(Blosc REQUIRED)

# add python module
pybind11_add_module(py_splv_encoder "src/brick.cpp" "src/py_splv_encoder.cpp" "src/splv_encoder.cpp")

if(WIN32)
    set_target_properties(py_splv_encoder PROPERTIES SUFFIX ".pyd")
endif()

# add include directories and libraries
include_directories("src/" ${pybind11_INCLUDE_DIRS})
# target_link_libraries(${PROJECT_NAME} blosc)
# Link against Blosc using the imported target
target_link_libraries(${PROJECT_NAME} PRIVATE Blosc::blosc)
target_link_libraries(py_splv_encoder PRIVATE Blosc::blosc pybind11::lto pybind11::embed pybind11::module)
