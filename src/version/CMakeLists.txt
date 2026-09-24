# Version-specific protocol implementation selected by ASCEMU_VERSION.

if (ASCEMU_VERSION STREQUAL "Midnight")
    # Midnight is fully self-contained in version_client, including World/,
    # packet handlers and profile-local BattleNetComm code.
    file(GLOB_RECURSE MIDNIGHT_VERSION_SRCS CONFIGURE_DEPENDS
        Midnight/*.cpp
        Midnight/*.h
        Midnight/*.hpp
        Midnight/*.inc
    )

    add_library(version_client STATIC ${MIDNIGHT_VERSION_SRCS})
    target_compile_definitions(version_client PUBLIC
        AE_HAS_WORLD_V2_PROFILE=1
        AE_WORLD_PROFILE_MIDNIGHT=1
        AE_WORLD_PROFILE_FOREVER=0
    )

elseif (ASCEMU_VERSION STREQUAL "Forever")
    # Forever is fully self-contained in version_client, including World/,
    # WorldSocket, VersionAdapter and its own BattleNetComm implementation.
    file(GLOB_RECURSE FOREVER_VERSION_SRCS CONFIGURE_DEPENDS
        Forever/*.cpp
        Forever/*.h
        Forever/*.hpp
        Forever/*.inc
    )

    add_library(version_client STATIC ${FOREVER_VERSION_SRCS})
    target_compile_definitions(version_client PUBLIC
        AE_HAS_WORLD_V2_PROFILE=1
        AE_WORLD_PROFILE_MIDNIGHT=0
        AE_WORLD_PROFILE_FOREVER=1
    )
else ()
    message(FATAL_ERROR "src/version was enabled for non-Battle.net ASCEMU_VERSION='${ASCEMU_VERSION}'")
endif ()

target_include_directories(version_client PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shared
    ${CMAKE_SOURCE_DIR}/src/world
)

# The modern version handlers are compiled inside version_client, but they use
# world headers whose public structures contain G3D types (for example
# MySQLStructures.h -> G3D/Quat.h). Since version_client is a separate target,
# it must receive the same G3D include path explicitly instead of relying on
# the world executable's private include directories.
target_include_directories(version_client SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/dep/g3dlite/include
)
# version_client contains modern packet handlers that use zlib directly
# (for example CMSG_UPDATE_ACCOUNT_DATA). shared links zlib PRIVATE, so its
# include path/link dependency is not propagated to version_client.
if (TARGET zlibstatic)
    set(VERSION_CLIENT_ZLIB_TARGET zlibstatic)
elseif (TARGET zlib)
    set(VERSION_CLIENT_ZLIB_TARGET zlib)
elseif (TARGET ZLIB::ZLIB)
    set(VERSION_CLIENT_ZLIB_TARGET ZLIB::ZLIB)
else ()
    message(FATAL_ERROR "version_client requires a zlib target")
endif ()

target_link_libraries(version_client
    PUBLIC
        shared
    PRIVATE
        ${VERSION_CLIENT_ZLIB_TARGET}
        g3dlite
)

unset(VERSION_CLIENT_ZLIB_TARGET)

# Create Visual Studio filters after all files are known to the target.
ascemu_source_group(${CMAKE_CURRENT_SOURCE_DIR})

unset(MIDNIGHT_VERSION_SRCS)
unset(FOREVER_VERSION_SRCS)
