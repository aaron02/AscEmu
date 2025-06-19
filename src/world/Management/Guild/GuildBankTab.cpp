/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "Guild.hpp"
#include "GuildBankTab.hpp"

#include "Logging/Logger.hpp"
#include "Objects/Item.hpp"
#include "Management/ObjectMgr.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Server/WorldSession.h"

#if VERSION_STRING < Cata
#include "Server/Packets/MsgQueryGuildBankText.h"
#else
#include "Server/Packets/SmsgGuildBankQueryTextResult.h"
#endif

using namespace AscEmu::Packets;

GuildBankTab::GuildBankTab(uint32_t guildId, uint8_t tabId) : mGuildId(guildId), mTabId(tabId)
{
    std::fill(mItems.begin(), mItems.end(), nullptr);
}

void GuildBankTab::loadGuildBankTabFromDB(Field* fields)
{
    mName = fields[2].asString();
    mIcon = fields[3].asString();
    mText = fields[4].asString();
}

bool GuildBankTab::loadGuildBankTabItemFromDB(Field* fields)
{
    uint8_t slotId = fields[2].asUint8();

    auto pItem = sObjectMgr.loadItem(fields[3].asUint32());
    if (pItem == nullptr)
    {
        auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_ITEM_DELETE_BY_ITEMGUID);
        stmt->Bind(0, fields[3].asUint32());                         // itemGuid
        stmt->Bind(1, mGuildId);                                     // guildId
        stmt->Bind(2, static_cast<uint32_t>(fields[1].asUint8()));   // tabId

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    mItems[slotId] = std::move(pItem);
    return true;
}

void GuildBankTab::removeBankTabItemFromDB(bool removeItemsFromDB)
{
    for (uint8_t slotId = 0; slotId < MAX_GUILD_BANK_SLOTS; ++slotId)
    {
        if (auto& pItem = mItems[slotId])
        {
            pItem->removeFromWorld();
            if (removeItemsFromDB)
                pItem->deleteFromDB();

            pItem = nullptr;
        }
    }
}

void GuildBankTab::writeInfoPacket(WorldPacket& data) const
{
#if VERSION_STRING < Cata
    uint8_t count = 0;

    size_t pos = data.wpos();
    data << uint8_t(0);

    for (uint8_t slotId = 0; slotId < MAX_GUILD_BANK_SLOTS; ++slotId)
        if (writeSlotPacket(data, slotId))
            ++count;

    data.put<uint8_t>(pos, count);
#else

    data << mName;
    data << mIcon;
#endif
}

bool GuildBankTab::writeSlotPacket(WorldPacket& data, uint8_t slotId, bool ignoreEmpty) const
{
    Item* pItem = getItem(slotId);
    const uint32_t itemEntry = pItem ? pItem->getEntry() : 0;

    if (!itemEntry && ignoreEmpty)
        return false;

    data << uint8_t(slotId);
    data << uint32_t(itemEntry);
    if (itemEntry)
    {
        data << uint32_t(0);

        if (uint32_t random = pItem->getRandomPropertiesId())
        {
            data << uint32_t(random);
            data << uint32_t(pItem->generateRandomSuffixFactor(pItem->getItemProperties()));
        }
        else
        {
            data << uint32_t(0);
        }

        data << uint32_t(pItem->getStackCount());
        data << uint32_t(0);
        data << uint8_t(abs(pItem->getSpellCharges(0)));

        uint8_t enchCount = 0;
        size_t enchCountPos = data.wpos();

        data << uint8_t(enchCount);
        for (uint32_t i = PERM_ENCHANTMENT_SLOT; i < MAX_ENCHANTMENT_SLOT; ++i)
        {
            if (uint32_t enchId = pItem->getEnchantmentId(static_cast<uint8_t>(EnchantmentSlot(i))))
            {
                data << uint8_t(i);
                data << uint32_t(enchId);
                ++enchCount;
            }
        }
        data.put<uint8_t>(enchCountPos, enchCount);
    }
    return true;
}

void GuildBankTab::setInfo(std::string const& name, std::string const& icon)
{
    if (mName == name && mIcon == icon)
        return;

    mName = name;
    mIcon = icon;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_TAB_UPDATE_NAME_ICON);
    stmt->Bind(0, mName);
    stmt->Bind(1, mIcon);
    stmt->Bind(2, mGuildId);
    stmt->Bind(3, static_cast<uint32_t>(mTabId));

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void GuildBankTab::setText(std::string const& text)
{
    if (mText == text)
        return;

    mText = text;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_TAB_UPDATE_TEXT_ICON);
    stmt->Bind(0, mText);
    stmt->Bind(1, mIcon);
    stmt->Bind(2, mGuildId);
    stmt->Bind(3, static_cast<uint32_t>(mTabId));

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void GuildBankTab::sendText(Guild const* guild, WorldSession* session) const
{
    if (session)
        sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "sendText % s: Tabid: % u, Text : % s", session->GetPlayer()->getName(), static_cast<uint32_t>(mTabId), mText);
    else
        sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "sendText (Broadcast): Tabid: {}, Text: {}", static_cast<uint32_t>(mTabId), mText);

#if VERSION_STRING < Cata
    if (session)
        session->SendPacket(MsgQueryGuildBankText(mTabId, mText).serialise().get());
    else
        guild->broadcastPacket(MsgQueryGuildBankText(mTabId, mText).serialise().get());
#else
    if (session)
        session->SendPacket(SmsgGuildBankQueryTextResult(mTabId, mText).serialise().get());
    else
        guild->broadcastPacket(SmsgGuildBankQueryTextResult(mTabId, mText).serialise().get());
#endif
}

std::string const& GuildBankTab::getName() const
{
    return mName;
}

std::string const& GuildBankTab::getIcon() const
{
    return mIcon;
}

std::string const& GuildBankTab::getText() const
{
    return mText;
}

Item* GuildBankTab::getItem(uint8_t slotId) const
{
    return slotId < MAX_GUILD_BANK_SLOTS ? mItems[slotId].get() : nullptr;
}

std::unique_ptr<Item> GuildBankTab::getItemHolder(uint8_t slotId)
{
    if (slotId >= MAX_GUILD_BANK_SLOTS)
        return nullptr;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_ITEM_DELETE);
    stmt->Bind(0, mGuildId);
    stmt->Bind(1, static_cast<uint32_t>(mTabId));
    stmt->Bind(2, static_cast<uint32_t>(slotId));

    CharacterDatabase.ExecuteStatement(std::move(stmt));

    std::unique_ptr<Item> tmpHolder = std::move(mItems[slotId]);
    return tmpHolder;
}

bool GuildBankTab::setItem(uint8_t slotId, std::unique_ptr<Item> item)
{
    if (slotId >= MAX_GUILD_BANK_SLOTS && slotId != UNDEFINED_TAB_SLOT)
        return false;

    if (item != nullptr)
    {
        uint32_t slot_id = 0;
        const auto itemGuid = item->getGuidLow();
        if (slotId == 0 || slotId == UNDEFINED_TAB_SLOT)
        {
            for (uint8_t i = 0; i < MAX_GUILD_BANK_SLOTS; ++i)
            {
                if (mItems[i] == nullptr)
                {
                    slot_id = i;
                    break;
                }
            }
        }
        else
        {
            slot_id = slotId;
        }

        mItems[slot_id] = std::move(item);

        auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_ITEM_INSERT);
        stmt->Bind(0, mGuildId);
        stmt->Bind(1, static_cast<uint32_t>(mTabId));
        stmt->Bind(2, slot_id);
        stmt->Bind(3, itemGuid);

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }
    else
    {
        mItems[slotId] = nullptr;
        
        auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_ITEM_DELETE);
        stmt->Bind(0, mGuildId);
        stmt->Bind(1, static_cast<uint32_t>(mTabId));
        stmt->Bind(2, static_cast<uint32_t>(slotId));

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    return true;
}
