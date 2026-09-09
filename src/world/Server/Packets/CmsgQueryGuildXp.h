/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

namespace AscEmu::Packets
{
    class CmsgQueryGuildXp : public ManagedPacket
    {
    public:
        WoWGuid guildGuid;

        CmsgQueryGuildXp() : CmsgQueryGuildXp(0)
        {
        }

        CmsgQueryGuildXp(uint64_t guildGuid) :
            ManagedPacket(CMSG_QUERY_GUILD_XP, 8),
            guildGuid(guildGuid)
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
                guildGuid[5] = packet.readBit();
                guildGuid[6] = packet.readBit();
                guildGuid[0] = packet.readBit();
                guildGuid[1] = packet.readBit();
                guildGuid[3] = packet.readBit();
                guildGuid[7] = packet.readBit();
                guildGuid[4] = packet.readBit();
                guildGuid[2] = packet.readBit();

                packet.readByteSeq(guildGuid[4]);
                packet.readByteSeq(guildGuid[6]);
                packet.readByteSeq(guildGuid[3]);
                packet.readByteSeq(guildGuid[0]);
                packet.readByteSeq(guildGuid[7]);
                packet.readByteSeq(guildGuid[5]);
                packet.readByteSeq(guildGuid[2]);
                packet.readByteSeq(guildGuid[1]);

                return true;
            }
            else if (m_protocol.isCata())
            {
                guildGuid[2] = packet.readBit();
                guildGuid[1] = packet.readBit();
                guildGuid[0] = packet.readBit();
                guildGuid[5] = packet.readBit();
                guildGuid[4] = packet.readBit();
                guildGuid[7] = packet.readBit();
                guildGuid[6] = packet.readBit();
                guildGuid[3] = packet.readBit();

                packet.readByteSeq(guildGuid[7]);
                packet.readByteSeq(guildGuid[2]);
                packet.readByteSeq(guildGuid[3]);
                packet.readByteSeq(guildGuid[6]);
                packet.readByteSeq(guildGuid[1]);
                packet.readByteSeq(guildGuid[5]);
                packet.readByteSeq(guildGuid[0]);
                packet.readByteSeq(guildGuid[4]);

                return true;
            }

            return false;
        }
    };
}
