/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"

#include <array>
#include <cstdint>

namespace AscEmu::Packets
{
    // The client always sends 50 quest ids (unused slots are 0) followed by the number of used slots
    class CmsgQuestNpcQuery : public ManagedPacket
    {
    public:
        static constexpr uint32_t QUEST_ID_COUNT = 50;

        std::array<uint32_t, QUEST_ID_COUNT> questIds = {};
        uint32_t questCount = 0;

        CmsgQuestNpcQuery() :
            ManagedPacket(CMSG_QUEST_NPC_QUERY, 4 * QUEST_ID_COUNT + 4)
        {
        }

    protected:
        bool internalDeserialise(WorldPacket& packet) override
        {
            if (m_protocol.isMop())
            {
                for (uint32_t i = 0; i < QUEST_ID_COUNT; ++i)
                    packet >> questIds[i];

                packet >> questCount;
                return true;
            }

            return false;
        }
    };
}
