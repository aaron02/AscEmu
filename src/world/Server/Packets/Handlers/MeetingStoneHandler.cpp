/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "Logging/Logger.hpp"
#include "Management/Group.h"
#include "Management/MeetingStone/MeetingStoneMgr.hpp"
#include "Management/ObjectMgr.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Server/WorldSession.h"
#include "Server/Packets/CmsgSetLookingForGroup.h"
#include "Server/Packets/MsgLookingForGroup.h"
#include "Server/Packets/SmsgMeetingstoneJoinfailed.h"

using namespace AscEmu::Packets;

namespace
{
    // If the player is grouped and not the leader, their LFG/LFM registration is attributed to
    // the group leader's guid instead - a solo player (or a leader) is their own "leader".
    uint64_t resolveLeaderGuid(Player* player)
    {
        auto* const group = player->getGroup();
        if (group == nullptr)
            return player->getGuid();

        auto* const leaderInfo = group->GetLeader();
        if (leaderInfo == nullptr || leaderInfo->guid == player->getGuidLow())
            return player->getGuid();

        auto* const leader = sObjectMgr.getPlayer(leaderInfo->guid);
        return leader != nullptr ? leader->getGuid() : player->getGuid();
    }

    // Attributes the registration to the group leader and attaches the rest of the (online) group
    // members so the whole party gets matched as a unit.
    void populateGroupInfo(Player* player, MeetingStonePlayerInfo& info)
    {
        info.leaderGuid = player->getGuid();
        info.isLeader = true;

        auto* const group = player->getGroup();
        if (group == nullptr || group->isBGGroup() || group->IsFull())
            return;

        auto* const leaderInfo = group->GetLeader();
        if (leaderInfo == nullptr || leaderInfo->guid == player->getGuidLow())
            return;

        auto* const leader = sObjectMgr.getPlayer(leaderInfo->guid);
        if (leader == nullptr)
            return;

        info.leaderGuid = leader->getGuid();
        info.isLeader = false;

        for (auto* const memberInfo : group->GetSubGroup(0)->getGroupMembers())
        {
            if (memberInfo->guid == leaderInfo->guid)
                continue;

            if (auto* const member = sObjectMgr.getPlayer(memberInfo->guid))
            {
                MeetingStoneGroupMember groupMember;
                groupMember.guid = member->getGuid();
                groupMember.level = member->getLevel();
                info.members.push_back(groupMember);
            }
        }
    }
}

void WorldSession::handleSetLookingForGroupOpcode(WorldPacket& recvPacket)
{
    CmsgSetLookingForGroup srlPacket;
    if (!parsePacket(recvPacket, srlPacket))
        return;

    if (srlPacket.slot >= MEETINGSTONE_MAX_LFG_SLOTS)
        return;

    const uint16_t entry = static_cast<uint16_t>(srlPacket.data & 0xFFFF);
    const uint16_t type = static_cast<uint16_t>((srlPacket.data >> 24) & 0xFFFF);

    const uint64_t leaderGuid = resolveLeaderGuid(_player);
    if (sMeetingStoneQueue.getInfo(leaderGuid) != nullptr)
    {
        sMeetingStoneQueue.setLfgSlot(leaderGuid, static_cast<uint8_t>(srlPacket.slot), entry, type);
        return;
    }

    MeetingStonePlayerInfo info;
    populateGroupInfo(_player, info);
    info.group[srlPacket.slot].set(entry, type);
    info.team = _player->getTeam();
    info.level = _player->getLevel();
    info.zoneId = _player->getZoneId();
    info.autoJoin = m_meetingStoneAutoJoin;

    sMeetingStoneQueue.startLookingForGroup(std::move(info), _player->getGuid());
}

void WorldSession::handleClearLookingForGroupOpcode(WorldPacket& /*recvPacket*/)
{
    sMeetingStoneQueue.stopLookingForGroup(resolveLeaderGuid(_player), _player->getGuid());
}

void WorldSession::handleSetLookingForMoreOpcode(WorldPacket& recvPacket)
{
    uint32_t data = 0;
    recvPacket >> data;

    const uint16_t entry = static_cast<uint16_t>(data & 0xFFFF);
    const uint16_t type = static_cast<uint16_t>((data >> 24) & 0xFFFF);

    if (sMeetingStoneQueue.getInfo(_player->getGuid()) != nullptr)
    {
        sMeetingStoneQueue.setLfmData(_player->getGuid(), entry, type);
        return;
    }

    MeetingStonePlayerInfo info;
    info.leaderGuid = _player->getGuid();
    info.isLeader = true;
    info.more.set(entry, type);
    info.team = _player->getTeam();
    info.level = _player->getLevel();
    info.zoneId = _player->getZoneId();
    info.autoFill = m_meetingStoneAutoFill;

    sMeetingStoneQueue.startLookingForMore(std::move(info), _player->getGuid());
}

void WorldSession::handleClearLookingForMoreOpcode(WorldPacket& /*recvPacket*/)
{
    sMeetingStoneQueue.stopLookingForMore(_player->getGuid());
}

void WorldSession::handleLfgSetAutoJoinOpcode(WorldPacket& /*recvPacket*/)
{
    m_meetingStoneAutoJoin = true;
    sMeetingStoneQueue.setAutoJoin(_player->getGuid(), true);

    SmsgMeetingstoneJoinfailed managedPacket(MeetingstoneJoinFailReason::None);
    sendManagedPacket(managedPacket);
}

void WorldSession::handleLfgClearAutoJoinOpcode(WorldPacket& /*recvPacket*/)
{
    m_meetingStoneAutoJoin = false;
    sMeetingStoneQueue.setAutoJoin(_player->getGuid(), false);
}

void WorldSession::handleLfmSetAutoFillOpcode(WorldPacket& /*recvPacket*/)
{
    auto result = MeetingstoneJoinFailReason::None;

    if (auto* const group = _player->getGroup())
    {
        if (group->isRaidGroup())
            result = MeetingstoneJoinFailReason::RaidGroup;
        else if (group->GetLeader() == nullptr || group->GetLeader()->guid != _player->getGuidLow())
            result = MeetingstoneJoinFailReason::PartyLeader;
        else if (group->IsFull())
            result = MeetingstoneJoinFailReason::FullGroup;
    }

    if (result == MeetingstoneJoinFailReason::None)
    {
        m_meetingStoneAutoFill = true;
        sMeetingStoneQueue.setAutoFill(_player->getGuid(), true);
    }

    SmsgMeetingstoneJoinfailed managedPacket(result);
    sendManagedPacket(managedPacket);
}

void WorldSession::handleLfmClearAutoFillOpcode(WorldPacket& /*recvPacket*/)
{
    m_meetingStoneAutoFill = false;
    sMeetingStoneQueue.setAutoFill(_player->getGuid(), false);
}

void WorldSession::handleMeetingstoneInfoOpcode(WorldPacket& /*recvPacket*/)
{
    // Verified against the real TBC server behavior: the response to this opcode is an empty stub
    // that only logs - the physical meeting-stone summon-queue flow this opcode served in Classic
    // (CMSG_MEETINGSTONE_JOIN/LEAVE/INFO with a group+area-id queue) was replaced by the LFG/LFM
    // slot system below and TBC never wired this opcode to anything. Kept registered only so it
    // doesn't log as unhandled.
    sLogger.debugOpcode("Received CMSG_MEETINGSTONE_INFO from {}.", _player->getGuid());
}

void WorldSession::handleAcceptLfgMatchOpcode(WorldPacket& /*recvPacket*/)
{
    sMeetingStoneQueue.handlePendingJoin(_player->getGuid());
}

void WorldSession::handleDeclineLfgMatchOpcode(WorldPacket& /*recvPacket*/)
{
    sMeetingStoneQueue.handleDeclinePendingJoin(_player->getGuid());
}

void WorldSession::handleCancelPendingLfgOpcode(WorldPacket& /*recvPacket*/)
{
    // Real client purpose unclear - nothing to do server-side.
}

void WorldSession::handleMeetingStoneSetCommentOpcode(WorldPacket& recvPacket)
{
    std::string comment;
    recvPacket >> comment;

    sMeetingStoneQueue.setComment(_player->getGuid(), comment);
}

void WorldSession::handleMsgLookingForGroupOpcode(WorldPacket& recvPacket)
{
    MsgLookingForGroup srlPacket;
    if (!parsePacket(recvPacket, srlPacket))
        return;

    sMeetingStoneQueue.sendListQueryResponse(_player->getGuid(), _player->getTeam(), srlPacket.queryType, srlPacket.queryEntry);
}
