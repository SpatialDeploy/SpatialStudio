# FindBlosc.cmake
# A simplified Find module for Blosc. This attempts to locate the blosc.h header
# and the blosc library. If found, it creates an imported target: Blosc::blosc.
#
# Variables provided upon success:
#   Blosc_FOUND - True if found.
#   Blosc_INCLUDE_DIRS - Include directory for Blosc.
#   Blosc_LIBRARIES - The Blosc library to link against.
#
# Imported Targets:
#   Blosc::blosc
#
# Usage:
#   find_package(Blosc REQUIRED)
#
# If found, you can link against it using:
#   target_link_libraries(your_target PUBLIC Blosc::blosc)

find_path(BLOSC_INCLUDE_DIR NAMES blosc.h)
find_library(BLOSC_LIBRARY NAMES blosc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Blosc
  REQUIRED_VARS BLOSC_INCLUDE_DIR BLOSC_LIBRARY
  FAIL_MESSAGE "Could not find Blosc"
)

if(Blosc_FOUND)
    set(Blosc_INCLUDE_DIRS ${BLOSC_INCLUDE_DIR})
    set(Blosc_LIBRARIES ${BLOSC_LIBRARY})

    add_library(Blosc::blosc UNKNOWN IMPORTED)
    set_target_properties(Blosc::blosc PROPERTIES
        IMPORTED_LOCATION ${BLOSC_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${Blosc_INCLUDE_DIRS}
    )

    message(STATUS "Searching for Blosc in ${BLOSC_INCLUDE_DIR}")
    message(STATUS "Searching for Blosc library in ${BLOSC_LIBRARY}")
endif()
