/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // TBC-only. Sent to the LFG player being matched into an LFM leader's group; the leader gets
    // the SmsgLfgPendingMatch counterpart, and the client answers via CMSG_ACCEPT_LFG_MATCH/
    // CMSG_DECLINE_LFG_MATCH. Classic has no equivalent.
    class SmsgLfgPendingInvite : public ManagedPacket
    {
    public:
        uint32_t entry = 0;

        SmsgLfgPendingInvite() : SmsgLfgPendingInvite(0)
        {
        }

        explicit SmsgLfgPendingInvite(uint32_t entry) :
            ManagedPacket(SMSG_LFG_PENDING_INVITE, 4),
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
