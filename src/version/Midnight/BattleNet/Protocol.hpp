#pragma once
#include <cstdint>
#include <string_view>
namespace AscEmu::Version::Midnight::BattleNet
{
    inline constexpr std::string_view WorldServerInitializer = "WORLD OF WARCRAFT CONNECTION - SERVER TO CLIENT - V2\n";
    inline constexpr std::string_view WorldClientInitializer = "WORLD OF WARCRAFT CONNECTION - CLIENT TO SERVER - V2\n";
    inline constexpr uint32_t AuthTagSize = 12;
    inline constexpr uint32_t ServerHeaderSize = 4 + AuthTagSize;
    inline constexpr uint32_t ClientHeaderSize = ServerHeaderSize + 4;
    inline constexpr uint32_t MaxPacketSize = 0x10000;
    inline constexpr uint32_t AuthChallengePayloadSize = 65;
    inline constexpr uint32_t AuthSessionFixedSize = 77;
    inline constexpr uint32_t ClientIvMagic = 0x544E4C43;
    inline constexpr uint32_t ServerIvMagic = 0x52565253;
}
