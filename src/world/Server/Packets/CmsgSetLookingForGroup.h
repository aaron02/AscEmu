/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // TBC-only "Looking For Group" opcode (predates the WotLK+ Dungeon Finder). Classic has no
    // equivalent handler - its meeting stones use a different, simpler group+area-id mechanic.
    class CmsgSetLookingForGroup : public ManagedPacket
    {
    public:
        uint32_t slot = 0;
        uint32_t data = 0;

        CmsgSetLookingForGroup() : CmsgSetLookingForGroup(0, 0)
        {
        }

        CmsgSetLookingForGroup(uint32_t slot, uint32_t data) :
            ManagedPacket(CMSG_SET_LOOKING_FOR_GROUP, 8),
            slot(slot),
            data(data)
        {
        }

    protected:
        bool internalDeserialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet >> slot >> data;
            return true;
        }
    };
}
