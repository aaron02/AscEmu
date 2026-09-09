/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

namespace AscEmu::Packets
{
    class CmsgGuildQueryNews : public ManagedPacket
    {
    public:
        CmsgGuildQueryNews() : ManagedPacket(CMSG_GUILD_QUERY_NEWS, 4)
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
                return 0; // Mop sends no payload at all
            return 0;
        }

        bool internalDeserialise(WorldPacket& packet) override
        {
            if (m_protocol.isCata())
            {
                packet.readSkip<uint32_t>();

                return true;
            }
            else if (m_protocol.isMop())
            {
                // Mop's client sends no payload for this opcode; nothing to read.
                return true;
            }

            return false;
        }
    };
}
