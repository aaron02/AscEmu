/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>
#include <vector>

namespace AscEmu::Packets
{
    class CmsgLearnTalent : public ManagedPacket
    {
    public:
        uint32_t talentId;
        uint32_t requestedRank;

        // Mop replaced the ranked point-spend talent system with a per-tier binary choice, and the
        // client can request learning several tiers at once (e.g. after a level-up), so it sends a
        // list of plain talent IDs instead of a single {talentId, rank} pair.
        std::vector<uint32_t> talentIds;

        CmsgLearnTalent() : CmsgLearnTalent(0, 0)
        {
        }

        CmsgLearnTalent(uint32_t talentId, uint32_t requestedRank) :
            ManagedPacket(CMSG_LEARN_TALENT, 4),
            talentId(talentId),
            requestedRank(requestedRank)
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
                return 3; // 3 bytes holding the 23 bit talent count
            return 0;
        }

        bool internalDeserialise(WorldPacket& packet) override
        {
            if (m_protocol.isMop())
            {
                const auto talentCount = packet.readBits(23);
                talentIds.reserve(talentCount);

                for (uint32_t i = 0; i < talentCount; ++i)
                {
                    uint16_t id = 0;
                    packet >> id;
                    talentIds.push_back(id);
                }

                return true;
            }
            else if (m_protocol.expansion <= WoW::Expansion::_Cata)
            {
                packet >> talentId >> requestedRank;

                return true;
            }

            return false;
        }
    };
}
