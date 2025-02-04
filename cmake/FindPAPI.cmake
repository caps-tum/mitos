# Find PAPI manually
find_path(PAPI_INCLUDE_DIR papi.h
  HINTS ENV PAPI_ROOT
  PATH_SUFFIXES include)

find_library(PAPI_LIBRARY NAMES papi
  HINTS ENV PAPI_ROOT
  PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PAPI DEFAULT_MSG PAPI_LIBRARY PAPI_INCLUDE_DIR)

if(PAPI_FOUND)
  add_library(PAPI::PAPI UNKNOWN IMPORTED)
  set_target_properties(PAPI::PAPI PROPERTIES
    IMPORTED_LOCATION "${PAPI_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PAPI_INCLUDE_DIR}"
  )
endif()