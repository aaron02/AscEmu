/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/
#pragma once

#include <cstdint>
#include <string_view>

namespace AscEmu::Version::Forever
{
    struct BuildProfile
    {
        uint32_t build;
        std::string_view name;
        bool supported;
    };

    // No public Forever client build has been pinned yet. Do not copy Midnight
    // opcodes or auth constants here: add an exact profile once the client is
    // available and verified.
    inline constexpr BuildProfile ActiveBuild{0U, "Forever", false};
    inline constexpr bool supportsBuild(uint32_t build)
    {
        return ActiveBuild.supported && build == ActiveBuild.build;
    }
}
