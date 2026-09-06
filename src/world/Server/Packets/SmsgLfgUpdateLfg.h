/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include "Management/MeetingStone/MeetingStoneMgr.hpp"
#include <cstdint>

namespace AscEmu::Packets
{
    // TBC-only. Not to be confused with the WotLK+ Dungeon Finder's SMSG_LFG_UPDATE_STATUS;
    // Classic has no equivalent packet at all.
    class SmsgLfgUpdateLfg : public ManagedPacket
    {
    public:
        uint32_t slots[MEETINGSTONE_MAX_LFG_SLOTS] = { 0, 0, 0 };

        SmsgLfgUpdateLfg() : ManagedPacket(SMSG_LFG_UPDATE_LFG, 0)
        {
        }

    protected:
        size_t expectedSize() const override { return 4 * MEETINGSTONE_MAX_LFG_SLOTS; }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            for (auto const slotData : slots)
                packet << slotData;

            return true;
        }
    };
}
