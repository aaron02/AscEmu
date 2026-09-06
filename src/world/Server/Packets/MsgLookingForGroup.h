/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include "WoWGuid.hpp"
#include "Management/MeetingStone/MeetingStoneMgr.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace AscEmu::Packets
{
    // TBC-only. Same opcode is used both ways: the client sends a query (type/entry/unk) and the
    // server answers on MSG_LOOKING_FOR_GROUP with the matching queued-player listing (the LFG
    // browser window). Classic has no equivalent.
    //
    // Player/member guids are packed here (unlike SmsgMeetingstoneMemberAdded's raw guid).
    struct MsgLookingForGroupEntry
    {
        uint64_t guid = 0;
        uint32_t level = 0;
        uint32_t zoneId = 0;
        bool isLFM = false;
        uint32_t lfmData = 0;                                  // more.entry | (more.type << 24), only sent if isLFM
        uint32_t lfgSlots[MEETINGSTONE_MAX_LFG_SLOTS] = { 0, 0, 0 }; // slot.entry | (slot.type << 24) per slot, only sent if !isLFM
        std::string comment;
        std::vector<std::pair<uint64_t, uint32_t>> members;    // guid, level - other members already in this entry's group
    };

    class MsgLookingForGroup : public ManagedPacket
    {
    public:
        // Query fields (populated by internalDeserialise)
        uint32_t queryType = 0;
        uint32_t queryEntry = 0;
        uint32_t queryUnk = 0;

        // Response fields (used by internalSerialise) - caller is expected to have already
        // filtered entries down to the requester's own team, leaders only, not-full
        std::vector<MsgLookingForGroupEntry> entries;

        MsgLookingForGroup() : ManagedPacket(MSG_LOOKING_FOR_GROUP, 12)
        {
        }

        MsgLookingForGroup(uint32_t type, uint32_t entry, std::vector<MsgLookingForGroupEntry> entries) :
            ManagedPacket(MSG_LOOKING_FOR_GROUP, 0),
            queryType(type),
            queryEntry(entry),
            entries(std::move(entries))
        {
        }

    protected:
        size_t expectedSize() const override { return 16 + entries.size() * 48; }

        bool internalDeserialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet >> queryType >> queryEntry >> queryUnk;
            return true;
        }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet << queryType;
            packet << queryEntry;
            packet << uint32_t(0); // displayed count, backpatched below
            packet << uint32_t(0); // found count, backpatched below

            // Real client hard-limits how many entries it will render
            uint32_t displayed = 0;
            const uint32_t found = static_cast<uint32_t>(entries.size());

            for (auto const& entry : entries)
            {
                if (displayed >= 50)
                    break;

                ++displayed;

                packet << WoWGuid(entry.guid);
                packet << entry.level;
                packet << entry.zoneId;
                packet << uint8_t(entry.isLFM);

                if (entry.isLFM)
                {
                    packet << entry.lfmData;
                    packet << uint32_t(0x1000000);
                    packet << uint32_t(0x1000000);
                }
                else
                {
                    for (auto const slotData : entry.lfgSlots)
                        packet << slotData;
                }

                packet << entry.comment;

                packet << static_cast<uint32_t>(entry.members.size());
                for (auto const& [memberGuid, memberLevel] : entry.members)
                {
                    packet << WoWGuid(memberGuid);
                    packet << memberLevel;
                }
            }

            packet.put<uint32_t>(8, displayed);
            packet.put<uint32_t>(12, found);

            return true;
        }
    };
}
