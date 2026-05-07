cmake_minimum_required(VERSION 3.15)

project(nanoupnp VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(nanoupnp upnp.c)
add_library(nanoupnp::nanoupnp ALIAS nanoupnp)

target_include_directories(nanoupnp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(WIN32)
    target_link_libraries(nanoupnp PUBLIC ws2_32 iphlpapi)
endif()

set_target_properties(nanoupnp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME nanoupnp
)

if(WIN32)
    set_target_properties(nanoupnp PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

install(TARGETS nanoupnp
    EXPORT nanoupnpTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES upnp.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/nanoupnp)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/nanoupnpConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/nanoupnpConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/nanoupnpConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanoupnp"
)

install(EXPORT nanoupnpTargets
    FILE nanoupnpTargets.cmake
    NAMESPACE nanoupnp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanoupnp
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/nanoupnpConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/nanoupnpConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanoupnp"
)
