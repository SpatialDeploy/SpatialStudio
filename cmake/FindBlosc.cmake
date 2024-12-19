find_path(BLOSC_INCLUDE_DIR blosc.h PATHS /usr/include)
find_library(BLOSC_LIBRARY NAMES blosc PATHS /usr/lib/x86_64-linux-gnu)

if(BLOSC_INCLUDE_DIR AND BLOSC_LIBRARY)
    set(BLOSC_FOUND TRUE)
endif()

mark_as_advanced(BLOSC_INCLUDE_DIR BLOSC_LIBRARY)
