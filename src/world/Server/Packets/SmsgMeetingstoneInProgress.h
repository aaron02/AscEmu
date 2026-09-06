/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

namespace AscEmu::Packets
{
    // TBC-only, opcode-only packet: a match attempt was made but the party could not be completed
    // (e.g. target group filled up in the meantime). Classic has no equivalent - see
    // SmsgMeetingstoneComplete.h.
    class SmsgMeetingstoneInProgress : public ManagedPacket
    {
    public:
        SmsgMeetingstoneInProgress() : ManagedPacket(SMSG_MEETINGSTONE_IN_PROGRESS, 0)
        {
        }

    protected:
        bool internalSerialise(WorldPacket& /*packet*/) override
        {
            return m_protocol.isTbc();
        }
    };
}
