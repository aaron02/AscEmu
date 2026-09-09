/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    class CmsgSetFactionAtWar : public ManagedPacket
    {
    public:
        uint32_t id;
        uint8_t state;

        CmsgSetFactionAtWar() : CmsgSetFactionAtWar(0, 0)
        {
        }

        CmsgSetFactionAtWar(uint32_t id, uint8_t state) :
            ManagedPacket(CMSG_SET_FACTION_ATWAR, 5),
            id(id),
            state(state)
        {
        }

        bool deserialise(WorldPacket& packet) override
        {
            if (packet.remaining() < expectedSize())
                return false;

            return internalDeserialise(packet);
        }

    protected:
        size_t expectedSize() const override
        {
            if (m_protocol.expansion <= WoW::Expansion::_Cata)
                return m_minimum_size;
            else if (m_protocol.isMop())
                return 1; // uint8 faction index
            return 0;
        }

        bool internalDeserialise(WorldPacket& packet) override
        {
            if (m_protocol.expansion < WoW::Expansion::_Mop)
            {
                packet >> id >> state;
                return true;
            }
            else if (m_protocol.isMop())
            {
                // Mop only sends the faction index as a single byte - always declares war (no "make peace" via this opcode)
                uint8_t factionIndex = 0;
                packet >> factionIndex;
                id = factionIndex;
                state = 1;
                return true;
            }

            return false;
        }
    };
}
