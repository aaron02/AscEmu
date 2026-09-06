/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

namespace AscEmu::Packets
{
    // TBC-only, opcode-only packet: the meeting-stone LFG/LFM queue for this player is done
    // (matched into a full group). Classic never sends this opcode - it reports queue status
    // entirely through SMSG_MEETINGSTONE_SETQUEUE's richer status enum instead.
    class SmsgMeetingstoneComplete : public ManagedPacket
    {
    public:
        SmsgMeetingstoneComplete() : ManagedPacket(SMSG_MEETINGSTONE_COMPLETE, 0)
        {
        }

    protected:
        bool internalSerialise(WorldPacket& /*packet*/) override
        {
            return m_protocol.isTbc();
        }
    };
}
