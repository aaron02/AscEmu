/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // Wire format (uint32 + uint8) verified identical for both Classic and TBC - only the meaning
    // of the uint32 differs (LFG dungeon entry vs. area id). Gated to TBC only below because
    // that's the only side AscEmu currently sends it from (Player::sendMeetingStoneSetQueuePacket,
    // called only from MeetingStoneMgr.cpp); Classic's own group+area-id queue that would send
    // this is not implemented.
    class SmsgMeetingstoneSetQueue : public ManagedPacket
    {
    public:
        uint32_t dungeonId;
        uint8_t status;

        SmsgMeetingstoneSetQueue() : SmsgMeetingstoneSetQueue(0, 0)
        {
        }

        SmsgMeetingstoneSetQueue(uint32_t dungeonId, uint8_t status) :
            ManagedPacket(SMSG_MEETINGSTONE_SETQUEUE, 0),
            dungeonId(dungeonId),
            status(status)
        {
        }

    protected:
        size_t expectedSize() const override
        {
            return 4 + 1;
        }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet << dungeonId << status;
            return true;
        }

        bool internalDeserialise(WorldPacket& /*packet*/) override { return false; }
    };
}
