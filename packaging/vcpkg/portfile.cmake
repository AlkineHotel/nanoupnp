vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AlkineHotel/nanoupnp
    REF "v.0.2.71828"
    SHA512 e8884c4897481a9592eb0a38538de763946af0fb0b8faa7855499211340e5b0c8524caea437af5aa0b72be4bf23aa485e7aabfd8f6e0792e186cc0c62f7a4b35
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/nanoupnp)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL
    "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright
)
