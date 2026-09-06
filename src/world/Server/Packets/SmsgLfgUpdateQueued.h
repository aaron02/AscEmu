/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // TBC-only. Not to be confused with the WotLK+ Dungeon Finder; Classic has no equivalent
    // packet at all.
    class SmsgLfgUpdateQueued : public ManagedPacket
    {
    public:
        bool queued = false;

        SmsgLfgUpdateQueued() : SmsgLfgUpdateQueued(false)
        {
        }

        explicit SmsgLfgUpdateQueued(bool queued) :
            ManagedPacket(SMSG_LFG_UPDATE_QUEUED, 1),
            queued(queued)
        {
        }

    protected:
        size_t expectedSize() const override { return 1; }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet << uint8_t(queued);
            return true;
        }
    };
}
