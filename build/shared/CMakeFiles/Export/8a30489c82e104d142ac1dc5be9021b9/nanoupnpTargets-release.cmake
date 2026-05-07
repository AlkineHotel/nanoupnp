#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "nanoupnp::nanoupnp" for configuration "Release"
set_property(TARGET nanoupnp::nanoupnp APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(nanoupnp::nanoupnp PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libnanoupnp.so.0.1.0"
  IMPORTED_SONAME_RELEASE "libnanoupnp.so.0"
  )

list(APPEND _cmake_import_check_targets nanoupnp::nanoupnp )
list(APPEND _cmake_import_check_files_for_nanoupnp::nanoupnp "${_IMPORT_PREFIX}/lib/libnanoupnp.so.0.1.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
