/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

namespace AscEmu::Packets
{
    class CmsgDuelResponse : public ManagedPacket
    {
    public:
        WoWGuid guid;
        bool accepted = false;

        CmsgDuelResponse() : ManagedPacket(CMSG_DUEL_RESPONSE, 8)
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
                return 2; // 2 bytes holding the guid + accepted bits
            return 0;
        }

        bool internalDeserialise(WorldPacket& packet) override
        {
            if (m_protocol.isMop())
            {
                guid[7] = packet.readBit();
                guid[1] = packet.readBit();
                guid[3] = packet.readBit();
                guid[4] = packet.readBit();
                guid[0] = packet.readBit();
                guid[2] = packet.readBit();
                guid[6] = packet.readBit();

                accepted = packet.readBit();

                guid[5] = packet.readBit();

                packet.readByteSeq(guid[6]);
                packet.readByteSeq(guid[4]);
                packet.readByteSeq(guid[5]);
                packet.readByteSeq(guid[0]);
                packet.readByteSeq(guid[1]);
                packet.readByteSeq(guid[2]);
                packet.readByteSeq(guid[7]);
                packet.readByteSeq(guid[3]);

                return true;
            }

            return false;
        }
    };
}
