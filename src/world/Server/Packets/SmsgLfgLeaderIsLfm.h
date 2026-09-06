/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

namespace AscEmu::Packets
{
    // TBC-only, opcode-only packet: tells the client the group leader it tried to join is
    // registered LFM (so the client should invite instead of trying to queue as LFG under them).
    // Classic has no equivalent packet at all.
    class SmsgLfgLeaderIsLfm : public ManagedPacket
    {
    public:
        SmsgLfgLeaderIsLfm() : ManagedPacket(SMSG_LFG_LEADER_IS_LFM, 0)
        {
        }

    protected:
        bool internalSerialise(WorldPacket& /*packet*/) override
        {
            return m_protocol.isTbc();
        }
    };
}
