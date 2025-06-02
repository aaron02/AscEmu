/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#ifndef UNIX
#include <cmath>
#endif

#include "CreatureAISpells.h"

#include "Server/Script/CreatureAIScript.hpp"

CreatureAISpells::CreatureAISpells(SpellInfo const* spellInfo, float castChance, uint32_t targetType, uint32_t duration, uint32_t cooldown, bool forceRemove, bool isTriggered) :
    mDurationTimer(std::make_unique<Util::SmallTimeTracker>(0)), mCooldownTimer(std::make_unique<Util::SmallTimeTracker>(0))
{
    mSpellInfo = spellInfo;
    mCastChance = castChance;
    mTargetType = targetType;
    mDuration = duration;

    mCooldown = cooldown;
    mForceRemoveAura = forceRemove;
    mIsTriggered = isTriggered;

    mMaxStackCount = 1;
    mCastCount = 0;
    mMaxCount = 0;

    scriptType = onAIUpdate;

    mMinPositionRangeToCast = 0.0f;
    mMaxPositionRangeToCast = 0.0f;

    mMinHpRangeToCast = 0;
    mMaxHpRangeToCast = 100;

    spell_type = STYPE_NULL;

    if (mSpellInfo != nullptr)
    {
        mMinPositionRangeToCast = mSpellInfo->getMinRange();
        mMaxPositionRangeToCast = mSpellInfo->getMaxRange();
    }

    mAttackStopTimer = 0;

    mCustomTargetCreature = nullptr;
}

void CreatureAISpells::setdurationTimer(uint32_t durationTimer)
{
    mDurationTimer->resetInterval(durationTimer);
}

void CreatureAISpells::setCooldownTimer(uint32_t cooldownTimer)
{
    mCooldownTimer->resetInterval(cooldownTimer);
}

void CreatureAISpells::addDBEmote(uint32_t textId)
{
    MySQLStructure::NpcScriptText const* npcScriptText = sMySQLStore.getNpcScriptText(textId);
    if (npcScriptText != nullptr)
        addEmote(npcScriptText->text, npcScriptText->type, npcScriptText->sound);
    else
        sLogger.debug("A script tried to add a spell emote with {}! Id is not available in table npc_script_text.", textId);
}

void CreatureAISpells::addEmote(std::string pText, uint8_t pType, uint32_t pSoundId)
{
    if (!pText.empty() || pSoundId)
        mAISpellEmote.emplace_back(AISpellEmotes(pText, pType, pSoundId));
}

void CreatureAISpells::sendRandomEmote(Unit* creatureAI)
{
    if (!mAISpellEmote.empty() && creatureAI != nullptr)
    {
        sLogger.debug("AISpellEmotes::sendRandomEmote() : called");

        uint32_t randomUInt = (mAISpellEmote.size() > 1) ? Util::getRandomUInt(static_cast<uint32_t>(mAISpellEmote.size() - 1)) : 0;
        creatureAI->sendChatMessage(mAISpellEmote[randomUInt].mType, LANG_UNIVERSAL, mAISpellEmote[randomUInt].mText);

        if (mAISpellEmote[randomUInt].mSoundId != 0)
            creatureAI->PlaySoundToSet(mAISpellEmote[randomUInt].mSoundId);
    }
}

void CreatureAISpells::setMaxStackCount(uint32_t stackCount)
{
    mMaxStackCount = stackCount;
}

const uint32_t CreatureAISpells::getMaxStackCount()
{
    return mMaxStackCount;
}

void CreatureAISpells::setMaxCastCount(uint32_t castCount)
{
    mMaxCount = castCount;
}

const uint32_t CreatureAISpells::getMaxCastCount()
{
    return mMaxCount;
}

const uint32_t CreatureAISpells::getCastCount()
{
    return mCastCount;
}

const bool CreatureAISpells::isDistanceInRange(float targetDistance)
{
    if (targetDistance >= mMinPositionRangeToCast && targetDistance <= mMaxPositionRangeToCast)
        return true;

    return false;
}

void CreatureAISpells::setMinMaxDistance(float minDistance, float maxDistance)
{
    mMinPositionRangeToCast = minDistance;
    mMaxPositionRangeToCast = maxDistance;
}

const bool CreatureAISpells::isHpInPercentRange(float targetHp)
{
    if (targetHp >= mMinHpRangeToCast && targetHp <= mMaxHpRangeToCast)
        return true;

    return false;
}

void CreatureAISpells::setMinMaxPercentHp(float minHp, float maxHp)
{
    mMinHpRangeToCast = minHp;
    mMaxHpRangeToCast = maxHp;
}

void CreatureAISpells::setAvailableForScriptPhase(std::vector<uint32_t> phaseVector)
{
    for (const auto& phase : phaseVector)
    {
        mPhaseList.push_back(phase);
    }
}

bool CreatureAISpells::isAvailableForScriptPhase(uint32_t scriptPhase)
{
    if (mPhaseList.empty())
        return true;

    for (const auto& availablePhase : mPhaseList)
    {
        if (availablePhase == scriptPhase)
            return true;
    }

    return false;
}

void CreatureAISpells::setAttackStopTimer(uint32_t attackStopTime)
{
    mAttackStopTimer = attackStopTime;
}

uint32_t CreatureAISpells::getAttackStopTimer()
{
    return mAttackStopTimer;
}

void CreatureAISpells::setAnnouncement(std::string announcement)
{
    mAnnouncement = announcement;
}

void CreatureAISpells::sendAnnouncement(Unit* pUnit)
{
    if (!mAnnouncement.empty() && pUnit != nullptr)
    {
        sLogger.debug("AISpellEmotes::sendAnnouncement() : called");

        pUnit->sendChatMessage(CHAT_MSG_RAID_BOSS_EMOTE, LANG_UNIVERSAL, mAnnouncement);
    }
}

void CreatureAISpells::setCustomTarget(Unit* targetCreature)
{
    mCustomTargetCreature = targetCreature;
}

Unit* CreatureAISpells::getCustomTarget()
{
    return mCustomTargetCreature;
}
