/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // Sent in response to CMSG_LFG_SET_AUTOJOIN/CMSG_LFM_SET_AUTOFILL (TBC) when the player's
    // current group state doesn't allow it (not leader, raid group, or already full).
    //
    // Wire format and reason values verified identical for both Classic and TBC, though Classic
    // triggers it from CMSG_MEETINGSTONE_JOIN instead. Gated to TBC only below because that's the
    // only side AscEmu currently wires up - Classic's own CMSG_MEETINGSTONE_JOIN handler
    // (group+area-id queueing) is not implemented.
    enum class MeetingstoneJoinFailReason : uint8_t
    {
        None         = 0,
        PartyLeader  = 1,
        FullGroup    = 2,
        RaidGroup    = 3
    };

    class SmsgMeetingstoneJoinfailed : public ManagedPacket
    {
    public:
        MeetingstoneJoinFailReason reason = MeetingstoneJoinFailReason::None;

        SmsgMeetingstoneJoinfailed() : SmsgMeetingstoneJoinfailed(MeetingstoneJoinFailReason::None)
        {
        }

        explicit SmsgMeetingstoneJoinfailed(MeetingstoneJoinFailReason reason) :
            ManagedPacket(SMSG_MEETINGSTONE_JOINFAILED, 1),
            reason(reason)
        {
        }

    protected:
        size_t expectedSize() const override { return 1; }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet << static_cast<uint8_t>(reason);
            return true;
        }
    };
}
