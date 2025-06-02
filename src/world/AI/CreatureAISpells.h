/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI_Defines.h"

class SERVER_DECL CreatureAISpells
{
public:
    CreatureAISpells(SpellInfo const* spellInfo, float castChance, uint32_t targetType, uint32_t duration, uint32_t cooldown, bool forceRemove, bool isTriggered);
    ~CreatureAISpells() = default;

    SpellInfo const* mSpellInfo;
    float mCastChance;
    uint32_t mTargetType;
    uint8_t scriptType;

    std::function<Unit* ()> getTargetFunction = nullptr;

    std::unique_ptr<Util::SmallTimeTracker> mDurationTimer;
    std::unique_ptr<Util::SmallTimeTracker> mCooldownTimer;

    uint32_t mDuration;
    void setdurationTimer(uint32_t durationTimer);
    void setCooldownTimer(uint32_t cooldownTimer);
    uint32_t mCooldown;

    uint32_t mMaxCount;
    uint32_t mCastCount;
    void setMaxCastCount(uint32_t castCount);
    const uint32_t getMaxCastCount();
    const uint32_t getCastCount();
    void setCastCount(uint32_t count) { mCastCount = count; }
    void increaseCastCount() { ++mCastCount; }

    bool mForceRemoveAura;
    bool mIsTriggered;

    AI_SpellType spell_type;

    //Zyres: temp boolean to determine if its coming from db or not
    bool fromDB = false;

    // non db script messages
    struct AISpellEmotes
    {
        AISpellEmotes(std::string pText, uint8_t pType, uint32_t pSoundId)
        {
            mText = (!pText.empty() ? pText : "");
            mType = pType;
            mSoundId = pSoundId;
        }

        std::string mText;
        uint8_t mType;
        uint32_t mSoundId;
    };
    typedef std::vector<AISpellEmotes> AISpellEmoteArray;
    AISpellEmoteArray mAISpellEmote;

    void addDBEmote(uint32_t textId);
    void addEmote(std::string pText, uint8_t pType = CHAT_MSG_MONSTER_YELL, uint32_t pSoundId = 0);

    void sendRandomEmote(Unit* creatureAI);

    uint32_t mMaxStackCount;

    void setMaxStackCount(uint32_t stackCount);
    const uint32_t getMaxStackCount();

    float mMinPositionRangeToCast;
    float mMaxPositionRangeToCast;

    const bool isDistanceInRange(float targetDistance);
    void setMinMaxDistance(float minDistance, float maxDistance);

    // if it is not a random target type it sets the hp range when the creature can cast this spell
    // if it is a random target it controles when the spell can be cast based on the target hp
    float mMinHpRangeToCast;
    float mMaxHpRangeToCast;

    const bool isHpInPercentRange(float targetHp);
    void setMinMaxPercentHp(float minHp, float maxHp);

    typedef std::vector<uint32_t> ScriptPhaseList;
    ScriptPhaseList mPhaseList;

    void setAvailableForScriptPhase(std::vector<uint32_t> phaseVector);
    bool isAvailableForScriptPhase(uint32_t scriptPhase);

    uint32_t mAttackStopTimer;
    void setAttackStopTimer(uint32_t attackStopTime);
    uint32_t getAttackStopTimer();

    std::string mAnnouncement;
    void setAnnouncement(std::string announcement);
    void sendAnnouncement(Unit* pUnit);

    Unit* mCustomTargetCreature;
    void setCustomTarget(Unit* targetCreature);
    Unit* getCustomTarget();
};
