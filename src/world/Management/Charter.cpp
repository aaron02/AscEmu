/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "Charter.hpp"

#include <sstream>

#include "Management/ObjectMgr.hpp"
#include "Database/Field.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Objects/Units/Players/PlayerDefines.hpp"
#include "Server/DatabaseDefinition.hpp"

Charter::Charter(Field const* _field)
{
    m_charterId = _field[0].asUint32();
    m_charterType = _field[1].asUint8();
    m_leaderGuid = _field[2].asUint32();
    m_guildName = _field[3].asString();
    m_itemGuid = _field[4].asUint64();

    m_availableSlots = getNumberOfAvailableSlots();

    for (uint8_t i = 0; i < m_availableSlots; ++i)
    {
        constexpr uint8_t fieldOffset = 5;

        if (uint32_t playerGuid = _field[i + fieldOffset].asUint32())
            m_signatures.push_back(playerGuid);
    }
}

Charter::Charter(uint32_t _id, uint32_t _leaderGuid, uint8_t _type) : m_charterId(_id), m_charterType(_type), m_leaderGuid(_leaderGuid)
{
    m_availableSlots = getNumberOfAvailableSlots();
}

Charter::~Charter() = default;

void Charter::saveToDB()
{
    auto stmt = CharacterDatabase.CreateStatement(CHAR_CHARTER_REPLACE);
    stmt->Bind(0, m_charterId);
    stmt->Bind(1, m_charterType);
    stmt->Bind(2, m_leaderGuid);
    stmt->Bind(3, m_guildName);
    stmt->Bind(4, m_itemGuid);

    size_t i = 5;
    for (const auto& guid : m_signatures)
        stmt->Bind(i++, guid);

    for (; i < 14; ++i)
        stmt->Bind(i, uint64_t(0));

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void Charter::destroy()
{
    auto stmt = CharacterDatabase.CreateStatement(CHAR_CHARTER_DELETE);
    stmt->Bind(0, m_charterId);
    CharacterDatabase.ExecuteStatement(std::move(stmt));

    for (const auto playerGuid : m_signatures)
    {
        if (Player* player = sObjectMgr.getPlayer(playerGuid))
            player->unsetCharter(m_charterType);
    }

    sObjectMgr.removeCharter(this);

    sObjectMgr.removeCharter(this);
}

uint32_t Charter::getLeaderGuid() const { return m_leaderGuid; }

uint32_t Charter::getId() const { return m_charterId; }

uint8_t Charter::getCharterType() const { return m_charterType; }

std::string Charter::getGuildName() { return m_guildName; }
void Charter::setGuildName(const std::string& _guildName) { m_guildName = _guildName; }

uint64_t Charter::getItemGuid() const { return m_itemGuid; }
void Charter::setItemGuid(const uint64_t _itemGuid) { m_itemGuid = _itemGuid; }

uint8_t Charter::getNumberOfAvailableSlots() const
{
    switch (m_charterType)
    {
        case CHARTER_TYPE_GUILD:
            return 9;
        case CHARTER_TYPE_ARENA_2V2:
            return 1;
        case CHARTER_TYPE_ARENA_3V3:
            return 2;
        case CHARTER_TYPE_ARENA_5V5:
            return 4;
        default:
            return 9;
    }
}
bool Charter::isFull() const { return m_signatures.size() == m_availableSlots; }
uint8_t Charter::getAvailableSlots() const { return m_availableSlots; }

void Charter::addSignature(uint32_t _playerGuid)
{
    if (m_signatures.size() >= m_availableSlots)
        return;

    m_signatures.push_back(_playerGuid);
}

void Charter::removeSignature(uint32_t _playerGuid)
{
    std::erase(m_signatures, _playerGuid);

    saveToDB();
}

uint8_t Charter::getSignatureCount() const { return static_cast<uint8_t>(m_signatures.size()); }
std::vector<uint32_t> Charter::getSignatures() { return m_signatures; }
