/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // TBC-only. Broadcast to a group when a new member is auto-matched into it via the
    // meeting-stone LFG/LFM queue. Classic never sends this opcode - see SmsgMeetingstoneComplete.h.
    //
    // The guid here is RAW (a plain 8-byte value), not the packed mask+bytes encoding AscEmu's
    // WoWGuid `operator<<` normally produces - verified against the real client protocol.
    class SmsgMeetingstoneMemberAdded : public ManagedPacket
    {
    public:
        uint64_t guid = 0;

        SmsgMeetingstoneMemberAdded() : SmsgMeetingstoneMemberAdded(0)
        {
        }

        explicit SmsgMeetingstoneMemberAdded(uint64_t guid) :
            ManagedPacket(SMSG_MEETINGSTONE_MEMBER_ADDED, 8),
            guid(guid)
        {
        }

    protected:
        size_t expectedSize() const override { return 8; }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet << guid;
            return true;
        }
    };
}
