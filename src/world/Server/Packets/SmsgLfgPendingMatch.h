/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // TBC-only. Sent to the LFM leader when the queue matched an LFG player into their group; the
    // joining player gets the SmsgLfgPendingInvite counterpart. Classic has no equivalent.
    class SmsgLfgPendingMatch : public ManagedPacket
    {
    public:
        uint32_t entry = 0;

        SmsgLfgPendingMatch() : SmsgLfgPendingMatch(0)
        {
        }

        explicit SmsgLfgPendingMatch(uint32_t entry) :
            ManagedPacket(SMSG_LFG_PENDING_MATCH, 4),
            entry(entry)
        {
        }

    protected:
        size_t expectedSize() const override { return 4; }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet << entry;
            return true;
        }
    };
}
