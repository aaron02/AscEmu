#pragma once

#include "version/Midnight/Packets/CharacterPackets.hpp"

class WorldSocket;

namespace AscEmu::Version::Midnight
{
    class CharacterService final
    {
    public:
        static CharacterService& instance();

        void requestCharacterEnum(WorldSocket& socket);
        bool createCharacter(WorldSocket& socket, const Packets::CmsgCreateCharacter& request);
    };
}
