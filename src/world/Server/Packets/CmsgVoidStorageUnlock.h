/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    class CmsgVoidStorageUnlock : public ManagedPacket
    {
    public:
        WoWGuid npcGuid;

        CmsgVoidStorageUnlock() :
            ManagedPacket(CMSG_VOID_STORAGE_UNLOCK, 8)
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
                npcGuid[3] = packet.readBit();
                npcGuid[1] = packet.readBit();
                npcGuid[5] = packet.readBit();
                npcGuid[6] = packet.readBit();
                npcGuid[4] = packet.readBit();
                npcGuid[0] = packet.readBit();
                npcGuid[7] = packet.readBit();
                npcGuid[2] = packet.readBit();

                packet.readByteSeq(npcGuid[4]);
                packet.readByteSeq(npcGuid[3]);
                packet.readByteSeq(npcGuid[6]);
                packet.readByteSeq(npcGuid[2]);
                packet.readByteSeq(npcGuid[1]);
                packet.readByteSeq(npcGuid[5]);
                packet.readByteSeq(npcGuid[7]);
                packet.readByteSeq(npcGuid[0]);

                return true;
            }
            else if (m_protocol.isCata())
            {
                npcGuid[4] = packet.readBit();
                npcGuid[5] = packet.readBit();
                npcGuid[3] = packet.readBit();
                npcGuid[0] = packet.readBit();
                npcGuid[2] = packet.readBit();
                npcGuid[1] = packet.readBit();
                npcGuid[7] = packet.readBit();
                npcGuid[6] = packet.readBit();

                packet.readByteSeq(npcGuid[7]);
                packet.readByteSeq(npcGuid[1]);
                packet.readByteSeq(npcGuid[2]);
                packet.readByteSeq(npcGuid[3]);
                packet.readByteSeq(npcGuid[5]);
                packet.readByteSeq(npcGuid[0]);
                packet.readByteSeq(npcGuid[6]);
                packet.readByteSeq(npcGuid[4]);

                return true;
            }

            return false;
        }
    };
}
