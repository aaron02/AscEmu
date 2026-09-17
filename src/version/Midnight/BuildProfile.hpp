/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/
#pragma once
#include <cstdint>
#include <string_view>

namespace AscEmu::Version::Midnight
{
    struct BuildProfile
    {
        uint32_t build;
        uint8_t protocolExpansion;
        std::string_view name;
    };

    inline constexpr BuildProfile ActiveBuild{69814U, 11U, "Midnight 12.1.0"};
    inline constexpr uint32_t Build = ActiveBuild.build;
    inline constexpr bool supportsBuild(uint32_t build) { return build == ActiveBuild.build; }
}
