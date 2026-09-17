# Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>

# Keep the client-version cache entry authoritative for the complete server
# topology. Modern clients use Battle.net automatically; legacy clients use
# the classic logonserver. There is intentionally no separate user-facing
# BUILD_BATTLENET switch.
if (NOT DEFINED ASCEMU_VERSION OR ASCEMU_VERSION STREQUAL "")
    set(ASCEMU_VERSION "WotLK" CACHE STRING "AscEmu client version")
else ()
    set(ASCEMU_VERSION "${ASCEMU_VERSION}" CACHE STRING "AscEmu client version")
endif ()

set_property(CACHE ASCEMU_VERSION PROPERTY STRINGS
    Classic
    TBC
    WotLK
    Cata
    Mop
    Midnight
    Forever
)

set(AE_USES_BATTLENET OFF)
if (ASCEMU_VERSION STREQUAL "Midnight" OR ASCEMU_VERSION STREQUAL "Forever")
    set(AE_USES_BATTLENET ON)
endif ()

# Compatibility variable for existing subdirectories. Keep it internal so the
# CMake GUI cannot create an invalid combination such as Midnight without BNet.
set(BUILD_BATTLENET ${AE_USES_BATTLENET} CACHE INTERNAL
    "Battle.net is selected automatically by ASCEMU_VERSION" FORCE)

# Keep human-readable ASCEMU_VERSION names separate from preprocessor tokens.
# Modern profile names are also C++ namespaces, so defining a macro named
# Midnight/Forever would corrupt e.g. AscEmu::Version::Midnight.
if (ASCEMU_VERSION STREQUAL "Midnight")
    set(ASCEMU_VERSION_TOKEN "AE_PROFILE_MIDNIGHT")
elseif (ASCEMU_VERSION STREQUAL "Forever")
    set(ASCEMU_VERSION_TOKEN "AE_PROFILE_FOREVER")
else ()
    set(ASCEMU_VERSION_TOKEN "${ASCEMU_VERSION}")
endif ()

add_subdirectory(shared)

if (AE_USES_BATTLENET)
    message(STATUS "ASCEMU_VERSION=${ASCEMU_VERSION}: enabling Battle.net stack")
    add_subdirectory(version)
    add_subdirectory(battlenet)
else ()
    message(STATUS "ASCEMU_VERSION=${ASCEMU_VERSION}: enabling legacy logonserver")
    add_subdirectory(logonserver)
endif ()

add_subdirectory(world)
add_subdirectory(collision)

if (BUILD_TOOLS)
    add_subdirectory(tools)
endif ()

if (BUILD_EXTRAS)
    add_subdirectory(tools/extras)
endif ()
