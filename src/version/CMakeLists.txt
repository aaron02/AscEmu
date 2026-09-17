# Version-specific protocol implementation selected by ASCEMU_VERSION.

if (ASCEMU_VERSION STREQUAL "Midnight")
    # Keep the version target organised like the rest of AscEmu: headers are
    # part of the target as well, so CMake generators (especially Visual
    # Studio) display the complete source tree instead of cpp files only.
    file(GLOB MIDNIGHT_ROOT_FILES
        Midnight/*.cpp
        Midnight/*.h
        Midnight/*.hpp
    )
    file(GLOB MIDNIGHT_DEFINE_FILES
        Midnight/Defines/*.cpp
        Midnight/Defines/*.h
        Midnight/Defines/*.hpp
    )
    file(GLOB MIDNIGHT_PACKET_FILES
        Midnight/Packets/*.cpp
        Midnight/Packets/*.h
        Midnight/Packets/*.hpp
    )
    file(GLOB MIDNIGHT_BATTLENET_FILES
        Midnight/BattleNet/*.cpp
        Midnight/BattleNet/*.h
        Midnight/BattleNet/*.hpp
    )

    # World adapters/handlers intentionally belong to the world executable;
    # do not add Midnight/World or Midnight/Packets/Handlers here.
    set(MIDNIGHT_VERSION_SRCS
        ${MIDNIGHT_ROOT_FILES}
        ${MIDNIGHT_DEFINE_FILES}
        ${MIDNIGHT_PACKET_FILES}
        ${MIDNIGHT_BATTLENET_FILES}
    )

    add_library(version_client STATIC ${MIDNIGHT_VERSION_SRCS})

elseif (ASCEMU_VERSION STREQUAL "Forever")
    file(GLOB_RECURSE FOREVER_VERSION_SRCS
        Forever/*.cpp
        Forever/*.h
        Forever/*.hpp
    )

    # Forever is a Battle.net client too. Keep it as its own version target so
    # its protocol can evolve independently from Midnight instead of adding
    # build checks throughout the generic world core.
    add_library(version_client STATIC ${FOREVER_VERSION_SRCS})
else ()
    message(FATAL_ERROR "src/version was enabled for non-Battle.net ASCEMU_VERSION='${ASCEMU_VERSION}'")
endif ()

target_include_directories(version_client PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shared
)
target_link_libraries(version_client PUBLIC shared)

# Create Visual Studio filters after all files are known to the target.
ascemu_source_group(${CMAKE_CURRENT_SOURCE_DIR})

unset(MIDNIGHT_ROOT_FILES)
unset(MIDNIGHT_DEFINE_FILES)
unset(MIDNIGHT_PACKET_FILES)
unset(MIDNIGHT_BATTLENET_FILES)
unset(MIDNIGHT_VERSION_SRCS)
unset(FOREVER_VERSION_SRCS)
