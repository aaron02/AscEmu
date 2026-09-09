/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

namespace AscEmu::Packets
{
    class CmsgDuelProposed : public ManagedPacket
    {
    public:
        WoWGuid targetGuid;

        CmsgDuelProposed() : ManagedPacket(CMSG_DUEL_PROPOSED, 8)
        {
        }

        bool deserialise(WorldPacket& packet) override
        {
            if (packet.remaining() < expectedSize())
                return false;

            return internalDeserialise(packet);
        }

    protected:
        size_t expectedSize() const override
        {
            if (m_protocol.expansion <= WoW::Expansion::_Cata)
                return m_minimum_size;
            else if (m_protocol.isMop())
                return 1; // packed guid: mask byte + only the non-zero guid bytes (1..9 bytes)
            return 0;
        }

        bool internalDeserialise(WorldPacket& packet) override
        {
            if (m_protocol.isMop())
            {
                targetGuid[1] = packet.readBit();
                targetGuid[5] = packet.readBit();
                targetGuid[4] = packet.readBit();
                targetGuid[6] = packet.readBit();
                targetGuid[3] = packet.readBit();
                targetGuid[2] = packet.readBit();
                targetGuid[7] = packet.readBit();
                targetGuid[0] = packet.readBit();

                packet.readByteSeq(targetGuid[4]);
                packet.readByteSeq(targetGuid[2]);
                packet.readByteSeq(targetGuid[5]);
                packet.readByteSeq(targetGuid[7]);
                packet.readByteSeq(targetGuid[1]);
                packet.readByteSeq(targetGuid[3]);
                packet.readByteSeq(targetGuid[6]);
                packet.readByteSeq(targetGuid[0]);

                return true;
            }

            return false;
        }
    };
}
