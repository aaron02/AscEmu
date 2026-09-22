/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "ObjectUpdate.hpp"
#include "SelfUpdateTemplate69913.hpp"

#include "Data/WoWObject.hpp"
#include "Data/WoWUnit.hpp"
#include "version/Forever/Fields/ForeverUpdateFields.hpp"
#include "Network/ByteBuffer.hpp"
#include "Logging/Logger.hpp"

#include <algorithm>
#include <cstring>
#include <span>

namespace AscEmu::Version::Forever::ObjectUpdate
{
    namespace
    {
        constexpr uint8_t UPDATE_TYPE_CREATE_OBJECT_2 = 2;
        constexpr uint8_t OBJECT_TYPE_UNIT = 5;
        constexpr uint8_t OBJECT_TYPE_ACTIVE_PLAYER = 7;

        // 69913 minimal stationary-unit create movement profile.
        // The variable GUID and position/orientation are generated per object;
        // this suffix was observed byte-identical across multiple stationary
        // retail creatures in the Stormwind capture. Its individual fields are
        // intentionally left semantically unnamed until separately proven.
        inline constexpr std::array<uint8_t, 135> STATIONARY_UNIT_MOVEMENT_SUFFIX_69913 = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x80,0x3F,0x00,0x00,0x00,0x00,0x20,0x40,0x00,0x00,0x00,0x41,0x00,0x00,
            0x90,0x40,0x71,0x1C,0x97,0x40,0x00,0x00,0x20,0x40,0x00,0x00,0xE0,0x40,0x00,0x00,
            0x90,0x40,0xDB,0x0F,0x49,0x40,0xDB,0x0F,0x49,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
            0x80,0x3F,0x00,0x00,0x00,0x40,0x00,0x00,0x82,0x42,0x00,0x00,0x80,0x3F,0x00,0x00,
            0x40,0x40,0x00,0x00,0x20,0x41,0x00,0x00,0xC8,0x42,0xDB,0x0F,0xC9,0x3F,0xAA,0x61,
            0x1C,0x40,0xDB,0x0F,0x49,0x40,0xDB,0x0F,0xC9,0x40,0xDB,0x0F,0xC9,0x3F,0xE4,0xCB,
            0x96,0x40,0x00,0x00,0xF0,0x41,0x00,0x00,0xA0,0x42,0x00,0x00,0x30,0x40,0x00,0x00,
            0xE0,0x40,0xCD,0xCC,0xCC,0x3E,0x00
        };
    }

    namespace
    {
        void writeModernGuid(ByteBuffer& data, WoWGuid const& guid)
        {
            const std::vector<uint8_t> packed = guid.packModern();
            data.append(packed.data(), packed.size());
        }

        constexpr uint8_t SELF_FIELD_FLAGS_69913 = 0x07U;
        constexpr uint8_t FRAGMENT_CGOBJECT_69913 = 0x03U;
        constexpr uint8_t FRAGMENT_PLAYER_HOUSE_INFO_69913 = 0x21U;
        constexpr uint8_t FRAGMENT_PLAYER_INITIATIVE_69913 = 0x26U;
        constexpr uint8_t FRAGMENT_TAG_UNIT_69913 = 0xCCU;
        constexpr uint8_t FRAGMENT_TAG_PLAYER_69913 = 0xCDU;
        constexpr uint8_t FRAGMENT_END_69913 = 0xFFU;


        void writeEmptyPlayerHouseInfoComponentCreate69913(ByteBuffer& data)
        {
            // Forever 69913 empty 0x21 component layout observed in the capture.
            data << uint32_t(0); // Field_8 count (owner)
            data << uint32_t(0); // Houses count
            data << uint32_t(0); // Field_88 count (owner)
            data << uint32_t(0); // Field_C0 count (owner)
            data << uint32_t(0); // Field_F8 count (owner)
            data << uint32_t(0); // Field_130 count (owner)

            // Verified Forever 69913 empty house defaults.
            // These two scalar fields are 1 and -1 even with no house data.
            data << int32_t(1) << int32_t(-1) << uint32_t(0);
            data << uint8_t(0);

            data << uint8_t(0); // EditorMode

            // Empty NeighborhoodOwnershipTransfer.
            writeModernGuid(data, WoWGuid());
            writeModernGuid(data, WoWGuid());
            data << uint8_t(0); // empty ownership name

            writeModernGuid(data, WoWGuid()); // CurrentHouse
        }

        void writeEmptyPlayerInitiativeComponentCreate69913(ByteBuffer& data)
        {
            writeModernGuid(data, WoWGuid()); // NeighborhoodGUID

            // Empty PlayerInitiativeInfo.
            data << int64_t(0);
            data << int32_t(0) << int32_t(0) << int32_t(0);
            data << float(0.0f) << float(0.0f) << float(0.0f);

            data << uint32_t(0); // CompletedTasks count
            data << uint32_t(0); // CompletedInitiatives count
            data << uint32_t(0); // Houses set count (owner)
        }

        void writeSpellCastVisualCreate(ByteBuffer& data, Fields::SpellCastVisual const& fields)
        {
            data << fields.spellXSpellVisualId << fields.scriptVisualId;
        }

        void writeUnitChannelCreate(ByteBuffer& data, Fields::UnitChannel const& fields)
        {
            data << fields.spellId;
            writeSpellCastVisualCreate(data, fields.spellVisual);
            data << fields.startTimeMs << fields.duration;
        }

        void writeVisibleItemCreate(ByteBuffer& data, Fields::VisibleItem const& fields)
        {
            data << fields.itemId << fields.secondaryItemModifiedAppearanceId << fields.conditionalItemAppearanceId << fields.itemAppearanceModId << fields.itemVisual << fields.itemModifiedAppearanceId << fields.field69913 << fields.transmogSlotOption << fields.sheatheCategory;
            data.writeBit(fields.hasTransmog);
            data.writeBit(fields.hasIllusion);
            data.flushBits();
        }

        void writePassiveSpellHistoryCreate(ByteBuffer& data, Fields::PassiveSpellHistory const& fields)
        {
            data << fields.spellId << fields.auraSpellId;
        }

        void writeUnitAssistActionDataCreate(ByteBuffer& data, Fields::UnitAssistActionData const& fields)
        {
            data << fields.type << fields.virtualRealmAddress;
            data.writeBits(fields.playerName.size(), 6);
            data.flushBits();
            if (!fields.playerName.empty())
                data.append(reinterpret_cast<uint8_t const*>(fields.playerName.data()), fields.playerName.size());
        }
    }

    namespace
    {
        void writeDynamicRecord(ByteBuffer& data, Fields::DynamicRecord const& record)
        {
            if (!record.data.empty())
                data.append(record.data.data(), record.data.size());
        }

        template <typename K>
        void writeDynamicRecordMap(ByteBuffer& data, std::map<K, Fields::DynamicRecord> const& values)
        {
            data << uint32_t(values.size());
            for (auto const& [key, value] : values)
            {
                data << key;
                writeDynamicRecord(data, value);
            }
        }

        void writeCustomizationCreate(ByteBuffer& data, Fields::ChrCustomizationChoice const& value)
        {
            data << value.optionId << value.choiceId;
        }

        void writeQuestLogCreate(ByteBuffer& data, Fields::QuestLog const& value)
        {
            data << value.questId << value.stateFlags;
            for (int16_t progress : value.objectiveProgress)
                data << progress;
            data << value.endTime << value.objectiveFlags << value.enabledObjectivesMask;
        }

        void writeSkillInfoCreate(ByteBuffer& data, Fields::SkillInfo const& value)
        {
            for (std::size_t i = 0; i < value.skillLineId.size(); ++i)
                data << value.skillLineId[i] << value.skillStep[i] << value.skillRank[i] << value.skillStartingRank[i] << value.skillMaxRank[i] << value.skillTempBonus[i] << value.skillPermBonus[i];
        }

        void writeZonePlayerForcedReactionCreate(ByteBuffer& data, Fields::ZonePlayerForcedReaction const& value)
        {
            data << value.factionId << value.reaction;
        }

        void writeCtrOptionsCreate(ByteBuffer& data, Fields::CtrOptions const& value)
        {
            // Forever 1.60.1.69913 capture layout:
            //   uint32 ConditionalFlagsCount
            //   uint8  FactionGroup
            //   uint32 ChromieTimeExpansionMask
            //   uint32 ConditionalFlags[Count]
            //
            // The previous serializer wrote unknown69913 as an extra fixed
            // uint32. With Count=0 that accidentally preserved the expected
            // 13-byte size, while encoding the wrong structure.
            data << uint32_t(value.conditionalFlags.size())
                 << value.factionGroup
                 << value.chromieTimeExpansionMask;

            for (uint32_t flag : value.conditionalFlags)
                data << flag;
        }

        void writeDungeonScoreSummaryCreate(ByteBuffer& data, Fields::DungeonScoreSummary const& value)
        {
            data << value.overallScoreCurrentSeason << value.ladderScoreCurrentSeason << uint32_t(value.runs.size());
            for (Fields::DungeonScoreMapSummary const& run : value.runs)
            {
                data << run.challengeModeId << run.mapScore << run.bestRunLevel << run.bestRunDurationMs << run.unknown1110;
                data.writeBit(run.finishedSuccess);
                data.flushBits();
            }
        }

        void writeCustomTabardInfoCreate(ByteBuffer& data, Fields::CustomTabardInfo const& value)
        {
            data << value.emblemStyle << value.emblemColor << value.borderStyle << value.borderColor << value.backgroundColor;
        }

        void writeNpcAsPlayerInfoCreate(ByteBuffer& data, Fields::NpcAsPlayerInfo const& value)
        {
            data << value.field0 << value.characterLoadoutId << value.creatureId;
            data << value.locWorldSpace.x << value.locWorldSpace.y << value.locWorldSpace.z << value.facingWorldSpace;
            writeModernGuid(data, value.transportGuid);
        }

        void writeItemInstanceCreate(ByteBuffer& data, Fields::ItemInstance const& value)
        {
            data << value.itemId;
            data.writeBit(value.itemBonus.has_value());
            data.writeBits(value.modifications.size(), 7);
            data.flushBits();
            for (Fields::ItemInstanceMod const& mod : value.modifications)
                data << mod.type << mod.value;
            if (value.itemBonus)
            {
                data << value.itemBonus->context << uint32_t(value.itemBonus->bonusListIds.size());
                for (uint32_t bonus : value.itemBonus->bonusListIds)
                    data << bonus;
            }
        }

        void writeTransmogOutfitDataInfoCreate(ByteBuffer& data, Fields::TransmogOutfitDataInfo const& value)
        {
            data << value.setType << value.icon;
            data.writeBits(value.name.size(), 8);
            data.writeBit(value.situationsEnabled);
            data.flushBits();
            if (!value.name.empty())
                data.append(reinterpret_cast<uint8_t const*>(value.name.data()), value.name.size());
        }

        void writeTransmogOutfitSituationInfoCreate(ByteBuffer& data, Fields::TransmogOutfitSituationInfo const& value)
        {
            data << value.situationId << value.specId << value.loadoutId << value.equipmentSetId;
        }

        void writeTransmogOutfitSlotDataCreate(ByteBuffer& data, Fields::TransmogOutfitSlotData const& value)
        {
            data << value.slot << value.slotOption << value.sheatheCategory << value.itemModifiedAppearanceId << value.appearanceDisplayType;
            data << value.spellItemEnchantmentId << value.illusionDisplayType << value.flags;
        }

        void writeTransmogOutfitDataCreate(ByteBuffer& data, Fields::TransmogOutfitData const& value)
        {
            data << value.id;
            writeTransmogOutfitDataInfoCreate(data, value.outfitInfo);
            data << uint32_t(value.situations.size()) << uint32_t(value.slots.size()) << value.flags;
            for (Fields::TransmogOutfitSituationInfo const& situation : value.situations)
                writeTransmogOutfitSituationInfoCreate(data, situation);
            for (Fields::TransmogOutfitSlotData const& slot : value.slots)
                writeTransmogOutfitSlotDataCreate(data, slot);
        }

        void writeTransmogOutfitMetadataCreate(ByteBuffer& data, Fields::TransmogOutfitMetadata const& value)
        {
            data << value.situationTrigger << value.transmogOutfitId << value.stampedOptionMainHand << value.stampedOptionOffHand << value.costMod;
            data.writeBit(value.locked);
            data.flushBits();
        }

        bool hasRequiredPlayerOpaqueRecords(Fields::PlayerData const&)
        {
            return true;
        }

        bool hasRequiredActivePlayerOpaqueRecords(Fields::ActivePlayerData const&)
        {
            return true;
        }
    }

    void writeObjectDataCreate(ByteBuffer& data, Fields::ObjectData const& fields)
    {
        data << fields.entryId << fields.dynamicFlags << fields.scale;
    }

    void writeUnitDataCreate(ByteBuffer& data, Fields::UnitData const& fields, bool ownerVisible)
    {
        data << fields.displayId << fields.npcFlags << fields.npcFlags2 << fields.stateSpellVisualId << fields.stateAnimId << fields.stateAnimKitId;
        data << uint32_t(fields.stateWorldEffectIds.size()) << fields.stateWorldEffectsQuestObjectiveId << fields.spellOverrideNameId;

        for (uint32_t value : fields.stateWorldEffectIds)
            data << value;

        writeModernGuid(data, fields.charm);
        writeModernGuid(data, fields.summon);
        if (ownerVisible)
            writeModernGuid(data, fields.critter);
        writeModernGuid(data, fields.charmedBy);
        writeModernGuid(data, fields.summonedBy);
        writeModernGuid(data, fields.createdBy);
        writeModernGuid(data, fields.demonCreator);
        writeModernGuid(data, fields.lookAtControllerTarget);
        writeModernGuid(data, fields.target);
        writeModernGuid(data, fields.battlePetCompanionGuid);

        data << fields.battlePetDbId;
        writeModernGuid(data, fields.battlePetAttachedToDecorGuid);
        writeModernGuid(data, fields.battlePetDecorHouseGuid);
        writeUnitChannelCreate(data, fields.channelData);
        data << fields.spellEmpowerStage << fields.summonedByHomeRealm << fields.race << fields.classId << fields.playerClassId << fields.sex << fields.creatureType << fields.displayPower << fields.overrideDisplayPowerId << fields.health;

        for (std::size_t i = 0; i < fields.power.size(); ++i)
            data << fields.power[i] << fields.maxPower[i];

        // 1.60.1.69913 creature creates contain both regen arrays as part of the
        // fixed UnitData create payload as well; they are not owner-only on the wire.
        for (std::size_t i = 0; i < fields.powerRegenFlatModifier.size(); ++i)
            data << fields.powerRegenFlatModifier[i] << fields.powerRegenInterruptedFlatModifier[i];

        data << fields.maxHealth << fields.level << fields.effectiveLevel << fields.contentTuningId << fields.scalingLevelMin << fields.scalingLevelMax << fields.scalingLevelDelta << fields.scalingFactionGroup << fields.factionTemplate;

        // 1.60.1.69913: VirtualItems are part of the fixed UnitData prefix and
        // are written immediately after FactionTemplate, before UnitFlags.
        for (Fields::VisibleItem const& value : fields.virtualItems)
            writeVisibleItemCreate(data, value);
        // 1.60.1.69913 verified from retail creature creates:
        // UnitFlags, UnitFlags2, UnitFlags3, unknown uint32, AuraState.
        data << fields.unitFlags69913 << fields.unitFlags2_69913 << fields.unitFlags3_69913 << fields.unknownU32AfterUnitFlags3_69913 << fields.auraState69913;

        for (uint32_t value : fields.attackRoundBaseTime)
            data << value;

        if (ownerVisible)
            data << fields.rangedAttackRoundBaseTime;

        data << fields.boundingRadius << fields.combatReach << fields.displayScale << fields.creatureFamily << fields.overrideCreatureType << fields.nativeDisplayId << fields.nativeXDisplayScale << fields.mountDisplayId << fields.cosmeticMountDisplayId;

        if (ownerVisible)
            data << fields.minDamage69913 << fields.maxDamage69913 << fields.minOffHandDamage69913 << fields.maxOffHandDamage69913;

        data << fields.standState << fields.petTalentPoints << fields.visFlags << fields.animTier << fields.petNumber << fields.petNameTimestamp << fields.petExperience << fields.unknownAfterPetExperience69913 << fields.petNextLevelExperience;
        data << fields.modCastingSpeed << fields.modCastingSpeedNeg << fields.modSpellHaste << fields.modHaste << fields.modRangedHaste << fields.modHasteRegen << fields.unknownFloatAfterPet6_69913;
        data << fields.unknownI32AfterPet0_69913 << fields.unknownI32AfterPet1_69913;

        if (ownerVisible)
        {
            data << fields.unknownBeforeStats69913;

            for (std::size_t i = 0; i < fields.stats69913.size(); ++i)
                data << fields.stats69913[i] << fields.statPosBuff69913[i] << fields.statNegBuff69913[i] << fields.unknownI32Array3_69913[i];

            for (int32_t value : fields.resistances69913)
                data << value;

            for (std::size_t i = 0; i < fields.unknownI32Array5_69913.size(); ++i)
                data << fields.unknownI32Array5_69913[i] << fields.unknownI32Array6_69913[i];
        }

        data << fields.baseMana;
        if (ownerVisible)
            data << fields.baseHealth;

        data << fields.sheatheState << fields.pvpFlags << fields.petFlags << fields.shapeshiftForm;

        if (ownerVisible)
        {
            data << fields.unknownI32OwnerCombat0_69913 << fields.unknownI32OwnerCombat1_69913 << fields.unknownI32OwnerCombat2_69913 << fields.unknownFloatOwnerCombat0_69913 << fields.unknownI32OwnerCombat3_69913;
            data << fields.unknownBeforeRangedAttackPower69913A << fields.unknownBeforeRangedAttackPower69913B;
            data << fields.unknownI32OwnerCombat4_69913 << fields.unknownI32OwnerCombat5_69913 << fields.unknownI32OwnerCombat6_69913 << fields.unknownFloatOwnerCombat1_69913 << fields.unknownI32OwnerCombat7_69913;
            data << fields.unknownI32OwnerCombat8_69913 << fields.unknownI32OwnerCombat9_69913 << fields.unknownI32OwnerCombat10_69913 << fields.unknownI32OwnerCombat11_69913 << fields.unknownFloatOwnerCombat2_69913 << fields.unknownFloatOwnerCombat3_69913 << fields.unknownFloatOwnerCombat4_69913 << fields.unknownFloatOwnerCombat5_69913;
        }

        data << fields.unknownFloatAfterOwnerCombat0_69913 << fields.unknownFloatAfterOwnerCombat1_69913 << fields.unknownI32AfterOwnerCombat0_69913 << fields.unknownI32AfterOwnerCombat1_69913 << fields.unknownI32AfterOwnerCombat2_69913 << fields.unknownI32AfterOwnerCombat3_69913 << fields.unknownI32AfterOwnerCombat4_69913 << fields.unknownI32AfterOwnerCombat5_69913 << fields.unknownU32AfterOwnerCombat0_69913;
        data << fields.unknownI32AfterOwnerCombat6_69913 << fields.unknownI32AfterOwnerCombat7_69913 << fields.unknownI32AfterOwnerCombat8_69913 << fields.unknownI32AfterOwnerCombat9_69913 << fields.unknownI32AfterOwnerCombat10_69913 << fields.unknownI32AfterOwnerCombat11_69913 << fields.unknownI32AfterOwnerCombat12_69913;
        writeModernGuid(data, fields.unknownGuid0_69913);
        data << uint32_t(fields.passiveSpells.size()) << uint32_t(fields.worldEffects.size()) << uint32_t(fields.channelObjects.size());
        data << fields.unknownI32AfterGuid0_69913 << fields.unknownFloatAfterGuid0_69913 << fields.unknownI32AfterGuid1_69913 << fields.unknownI32AfterGuid2_69913 << fields.unknownI32AfterGuid3_69913 << fields.unknownU32AfterGuid0_69913;
        if (ownerVisible)
            data << fields.unknownBeforeCurrentAreaId69913;
        data << fields.currentAreaId << fields.nameplateDistanceMod << fields.autoAttackRangeMod;
        if (ownerVisible)
        {
            data.append(fields.ownerExtension69913.prefix.data(), fields.ownerExtension69913.prefix.size());
            writeModernGuid(data, fields.ownerExtension69913.guidA);
            writeModernGuid(data, fields.ownerExtension69913.guidB);
            data.append(fields.ownerExtension69913.suffix.data(), fields.ownerExtension69913.suffix.size());
        }
        writeModernGuid(data, fields.nameplateAttachToGuid);

        for (Fields::PassiveSpellHistory const& value : fields.passiveSpells)
            writePassiveSpellHistoryCreate(data, value);
        for (int32_t value : fields.worldEffects)
            data << value;
        for (WoWGuid const& value : fields.channelObjects)
            writeModernGuid(data, value);

        data.writeBit(fields.field314);
        data.writeBit(fields.unknownOptionalRecord0_69913.has_value());
        data.flushBits();
        if (fields.unknownOptionalRecord0_69913)
            writeUnitAssistActionDataCreate(data, *fields.unknownOptionalRecord0_69913);
    }

    bool writePlayerDataCreateImpl(ByteBuffer& data, Fields::PlayerData const& fields, bool partyMemberVisible)
    {
        if (!hasRequiredPlayerOpaqueRecords(fields))
            return false;
        writeModernGuid(data, fields.unknownGuid0_69913);
        writeModernGuid(data, fields.unknownGuid1_69913);
        writeModernGuid(data, fields.unknownGuid2_69913);
        data << fields.unknownU64_0_69913;
        writeModernGuid(data, fields.unknownGuid3_69913);
        data << fields.unknownU32_0_69913 << fields.unknownU32_1_69913 << fields.unknownU32_2_69913 << fields.unknownU32_3_69913 << fields.unknownI32_0_69913;
        data.append(fields.unknownBeforeCustomizationCounts69913.data(), fields.unknownBeforeCustomizationCounts69913.size());
        data << uint32_t(fields.customizations.size()) << uint32_t(fields.unknownCustomizationChoices0_69913.size());

        for (uint8_t value : fields.unknownBytes0_69913)
            data << value;

        data << fields.unknownU8_0_69913 << fields.unknownU8_1_69913 << fields.unknownU8_2_69913 << fields.unknownU8_3_69913 << fields.unknownU32_4_69913 << fields.unknownI32_1_69913;

        if (partyMemberVisible)
        {
            for (Fields::QuestLog const& value : fields.unknownPartyRecords0_69913)
                writeQuestLogCreate(data, value);
            data << uint32_t(fields.unknownPartyMap0_69913.size());
            for (auto const& [questId, index] : fields.unknownPartyMap0_69913)
                data << questId << index;
            data << uint32_t(fields.unknownPartyDynamicRecords0_69913.size());
        }

        for (Fields::VisibleItem const& value : fields.unknownVisibleItemRecords0_69913)
            writeVisibleItemCreate(data, value);

        data << fields.unknownI32_2_69913 << fields.unknownI32_3_69913 << fields.unknownU32_5_69913 << fields.unknownU32_6_69913 << fields.unknownI32_4_69913 << fields.unknownI32_5_69913;

        for (float value : fields.unknownFloatArray0_69913)
            data << value;

        data << fields.unknownU8_4_69913 << fields.unknownI32_6_69913 << fields.unknownI64_0_69913;
        data << uint32_t(fields.unknownDynamicRecords0_69913.size());

        for (Fields::ZonePlayerForcedReaction const& value : fields.unknownFixedRecords0_69913)
            writeZonePlayerForcedReactionCreate(data, value);

        data << fields.unknownI32_7_69913 << fields.unknownI32_8_69913 << fields.unknownI32_9_69913;
        data << uint32_t(fields.unknownDynamicRecords1_69913.size());
        writeCtrOptionsCreate(data, fields.unknownCtrOptions0_69913);
        data << fields.unknownI32_10_69913 << fields.unknownI32_11_69913;
        writeDungeonScoreSummaryCreate(data, fields.unknownDungeonScore0_69913);
        writeModernGuid(data, fields.unknownLeaverInfo0_69913.bnetAccountGuid);
        data << fields.unknownLeaverInfo0_69913.leaveScore << fields.unknownLeaverInfo0_69913.seasonId << fields.unknownLeaverInfo0_69913.totalLeaves
             << fields.unknownLeaverInfo0_69913.totalSuccesses << fields.unknownLeaverInfo0_69913.consecutiveSuccesses
             << fields.unknownLeaverInfo0_69913.lastPenaltyTime << fields.unknownLeaverInfo0_69913.leaverExpirationTime << fields.unknownLeaverInfo0_69913.flags;
        data.writeBit(fields.unknownLeaverInfo0_69913.isLeaver);
        data.flushBits();
        writeModernGuid(data, fields.unknownGuid4_69913);
        data << fields.unknownI32_12_69913;

        for (Fields::ItemInstance const& value : fields.unknownItemInstances0_69913)
            writeItemInstanceCreate(data, value);
        data << uint32_t(fields.unknownI32Vector0_69913.size());

        for (uint32_t value : fields.attackRoundBaseTime)
            data << value;

        writeCustomTabardInfoCreate(data, fields.unknownCustomTabard0_69913);
        writeNpcAsPlayerInfoCreate(data, fields.unknownNpcAsPlayer0_69913);
        data.append(fields.unknownBeforeCustomizationPayload69913.data(), fields.unknownBeforeCustomizationPayload69913.size());

        for (Fields::ChrCustomizationChoice const& value : fields.customizations)
            writeCustomizationCreate(data, value);
        for (Fields::ChrCustomizationChoice const& value : fields.unknownCustomizationChoices0_69913)
            writeCustomizationCreate(data, value);

        if (partyMemberVisible)
            for (Fields::QuestLog const& value : fields.unknownPartyDynamicRecords0_69913)
                writeQuestLogCreate(data, value);

        for (Fields::DynamicRecord const& value : fields.unknownDynamicRecords0_69913)
            writeDynamicRecord(data, value);
        for (Fields::DynamicRecord const& value : fields.unknownDynamicRecords1_69913)
            writeDynamicRecord(data, value);
        for (int32_t value : fields.unknownI32Vector0_69913)
            data << value;

        // Forever 1.60.1 build 69913 carries first and last name separately.
        //
        // Two independent captures prove the byte-aligned length encoding:
        //   Test / Hims   -> 10 08 00 + "TestHims"
        //   Schurki / Asc -> 1C 06 00 + "SchurkiAsc"
        //
        // byte0 = firstNameLength << 2  (6-bit length, byte-aligned)
        // byte1 = lastNameLength  << 1  (observed zero guard bits around a 6-bit length)
        // byte2 = currently-zero optional-name flags in both captures.
        //
        // Keep the third byte conservative until a capture with one of the optional
        // states set proves its individual bit assignments.
        const std::size_t firstNameLength = std::min<std::size_t>(fields.firstName.size(), 63U);
        const std::size_t lastNameLength = std::min<std::size_t>(fields.lastName.size(), 63U);

        data << static_cast<uint8_t>(firstNameLength << 2U);
        data << static_cast<uint8_t>(lastNameLength << 1U);

        uint8_t foreverNameFlags = 0;
        if (partyMemberVisible && fields.unknownNameFlag0_69913)
            foreverNameFlags |= 0x80U;
        if (fields.unknownNameFlag1_69913)
            foreverNameFlags |= 0x40U;
        if (fields.unknownOptionalNamePayload0_69913.has_value())
            foreverNameFlags |= 0x20U;
        data << foreverNameFlags;

        if (firstNameLength != 0U)
            data.append(reinterpret_cast<uint8_t const*>(fields.firstName.data()), firstNameLength);
        if (lastNameLength != 0U)
            data.append(reinterpret_cast<uint8_t const*>(fields.lastName.data()), lastNameLength);

        if (fields.unknownOptionalNamePayload0_69913)
            writeDynamicRecord(data, *fields.unknownOptionalNamePayload0_69913);
        return true;
    }

    bool writePlayerDataCreate(ByteBuffer& data, Fields::PlayerData const& fields, bool partyMemberVisible)
    {
        return writePlayerDataCreateImpl(data, fields, partyMemberVisible);
    }


    namespace
    {
        Fields::ActivePlayerData makeConservativeActivePlayerData69913(Fields::ActivePlayerData const& source)
        {
            // Forever 69913 live profile:
            // Keep only fields whose placement/encoding has been established from
            // our captures. Everything else intentionally stays at the protocol's
            // minimal zero/empty state until a Forever sniff proves its meaning.
            //
            // Unproven Midnight-era regions are represented only by zeroed
            // opaque Forever wire spans.
            Fields::ActivePlayerData result{};

            // Proven prefix. Packed GUID length is part of the wire layout, so keep
            // the real inventory/observer GUID state instead of capture bytes.
            result.invSlots = source.invSlots;
            result.farsightObject = source.farsightObject;
            result.summonedBattlePetGuid = source.summonedBattlePetGuid;
            result.knownTitles = source.knownTitles;
            result.coinage = source.coinage;
            result.accountBankCoinage = source.accountBankCoinage;
            result.xp = source.xp;
            result.nextLevelXp = source.nextLevelXp;

            // The following 32-bit slot exists on the wire immediately after
            // NextLevelXP, but its Forever semantics are not proven. Keep
            // unknownAfterNextLevelXp69913 at its zero default until a sniff
            // demonstrates what it represents.

            // Keep the complete unproven pre-transmog span at its conservative
            // zero-state.  The earlier +848 = 0x01 hypothesis is disproven by
            // the normalized empty-inventory comparison: generatedNonZero=1,
            // captureNonZero=53 and 54 mismatches show that marker was not one
            // of the capture non-zero bytes.

            // Proven Forever transmog-outfit library. Preserve only this known
            // island after the structurally valid minimal middle section.
            result.transmogOutfits = source.transmogOutfits;
            result.viewedOutfit = source.viewedOutfit;
            result.transmogMetadata = source.transmogMetadata;

            return result;
        }
    }

    namespace
    {
        struct ActivePlayerCreateLayout69913
        {
            std::size_t afterInvSlots = 0;
            std::size_t afterFarsight = 0;
            std::size_t afterSummonedBattlePet = 0;
            std::size_t afterCoreScalars = 0;
            std::size_t afterSkillInfo = 0;
            std::size_t afterUnknownAfterSkillInfoPrefix = 0;
            std::size_t afterUnknownAfterSkillInfoData = 0;
            std::size_t afterUnknownAfterSkillInfo = 0;
            std::size_t afterPostSkillBlockPrefix = 0;
            std::size_t afterPostSkillBlockData = 0;
            std::size_t afterPostCombatStats = 0;
            std::size_t afterUnknownBeforeTransmogPrefix = 0;
            std::size_t afterUnknownBeforeTransmog = 0;
            std::size_t afterTransmog = 0;
            std::size_t end = 0;
        };

        bool writeActivePlayerDataCreateImpl(ByteBuffer& data, Fields::ActivePlayerData const& fields, ActivePlayerCreateLayout69913* layout)
        {
            if (!hasRequiredActivePlayerOpaqueRecords(fields))
                return false;

            const std::size_t begin = data.size();
            const auto mark = [&](std::size_t& out)
            {
                out = data.size() - begin;
            };

            for (WoWGuid const& value : fields.invSlots)
                writeModernGuid(data, value);
            if (layout) mark(layout->afterInvSlots);

            writeModernGuid(data, fields.farsightObject);
            if (layout) mark(layout->afterFarsight);
            writeModernGuid(data, fields.summonedBattlePetGuid);
            if (layout) mark(layout->afterSummonedBattlePet);

            data << uint32_t(fields.knownTitles.size()) << fields.coinage << fields.accountBankCoinage << fields.xp << fields.nextLevelXp << fields.unknownAfterNextLevelXp69913;
            if (layout) mark(layout->afterCoreScalars);

            writeSkillInfoCreate(data, fields.skill);
            if (layout) mark(layout->afterSkillInfo);
            data.append(fields.unknownAfterSkillInfoPrefix69913.data(), fields.unknownAfterSkillInfoPrefix69913.size());
            if (layout) mark(layout->afterUnknownAfterSkillInfoPrefix);
            data.append(fields.unknownAfterSkillInfoData69913.data(), fields.unknownAfterSkillInfoData69913.size());
            if (layout) mark(layout->afterUnknownAfterSkillInfoData);
            data.append(fields.unknownAfterSkillInfoSuffix69913.data(), fields.unknownAfterSkillInfoSuffix69913.size());
            if (layout) mark(layout->afterUnknownAfterSkillInfo);

            data.append(fields.unknownPostSkillBlockPrefix69913.data(), fields.unknownPostSkillBlockPrefix69913.size());
            if (layout) mark(layout->afterPostSkillBlockPrefix);
            data.append(fields.unknownPostSkillBlockData69913.data(), fields.unknownPostSkillBlockData69913.size());
            if (layout) mark(layout->afterPostSkillBlockData);
            data.append(fields.unknownPostSkillBlockSuffix69913.data(), fields.unknownPostSkillBlockSuffix69913.size());
            if (layout) mark(layout->afterPostCombatStats);

            data.append(fields.unknownBeforeTransmogPrefix69913.data(), fields.unknownBeforeTransmogPrefix69913.size());
            if (layout) mark(layout->afterUnknownBeforeTransmogPrefix);
            data.append(fields.unknownBeforeTransmogData69913.data(), fields.unknownBeforeTransmogData69913.size());
            data.append(fields.unknownBeforeTransmogSuffix69913.data(), fields.unknownBeforeTransmogSuffix69913.size());
            if (layout) mark(layout->afterUnknownBeforeTransmog);

            writeDynamicRecordMap(data, fields.transmogOutfits);
            writeTransmogOutfitDataCreate(data, fields.viewedOutfit);
            writeTransmogOutfitMetadataCreate(data, fields.transmogMetadata);
            data.flushBits();
            if (layout) mark(layout->afterTransmog);

            for (uint64_t value : fields.knownTitles)
                data << value;

            data.append(fields.unknownAfterTransmog69913.data(), fields.unknownAfterTransmog69913.size());
            if (layout) mark(layout->end);

            return true;
        }
    }

    bool writeActivePlayerDataCreate(ByteBuffer& data, Fields::ActivePlayerData const& fields)
    {
        return writeActivePlayerDataCreateImpl(data, fields, nullptr);
    }



    namespace
    {
        void writeStationaryUnitMovement69913(
            ByteBuffer& data, std::span<const uint8_t> packedGuid,
            float x, float y, float z, float orientation, uint32_t movementTimeMs)
        {
            // Capture-proven minimal stationary creature layout:
            //   7-byte fixed prefix
            //   ModernGUID
            //   8-byte zero block
            //   movement timestamp (uint32 ms)
            //   x/y/z/orientation
            //   byte-stable stationary movement suffix
            static constexpr std::array<uint8_t, 7> prefix = { 0x84, 0, 0, 0, 0, 0, 0 };
            static constexpr std::array<uint8_t, 8> zeros = { 0, 0, 0, 0, 0, 0, 0, 0 };
            data.append(prefix.data(), prefix.size());
            data.append(packedGuid.data(), packedGuid.size());
            data.append(zeros.data(), zeros.size());
            data << movementTimeMs;
            data << x << y << z << orientation;
            data.append(STATIONARY_UNIT_MOVEMENT_SUFFIX_69913.data(), STATIONARY_UNIT_MOVEMENT_SUFFIX_69913.size());
        }
    }

    std::vector<uint8_t> buildCreatureCreateBlock69913(
        std::span<const uint8_t> packedGuid,
        float x, float y, float z, float orientation, uint32_t movementTimeMs,
        Fields::ObjectData const& objectFields,
        Fields::UnitData const& unitFields)
    {
        if (packedGuid.empty())
            return {};

        ByteBuffer fieldPayload;

        // Minimal Forever unit fragment set observed on ordinary stationary
        // creatures: flags=0x04, CGObject, Unit tag, end, CGObject active.
        fieldPayload << uint8_t(0x04)
                     << uint8_t(FRAGMENT_CGOBJECT_69913)
                     << uint8_t(FRAGMENT_TAG_UNIT_69913)
                     << uint8_t(FRAGMENT_END_69913)
                     << uint8_t(1);
        writeObjectDataCreate(fieldPayload, objectFields);
        writeUnitDataCreate(fieldPayload, unitFields, false);

        ByteBuffer block;
        block << uint8_t(1); // CREATE_OBJECT (ordinary world unit)
        block.append(packedGuid.data(), packedGuid.size());
        block << uint8_t(OBJECT_TYPE_UNIT);
        writeStationaryUnitMovement69913(block, packedGuid, x, y, z, orientation, movementTimeMs);
        block << uint32_t(fieldPayload.size());
        block.append(fieldPayload);

        return std::vector<uint8_t>(block.contents(), block.contents() + block.size());
    }

    std::vector<uint8_t> buildHybridSelfFieldPayload69913(
        Fields::ObjectData const& objectFields,
        Fields::UnitData const& unitFields,
        Fields::PlayerData const& playerFields,
        Fields::ActivePlayerData const& activePlayerFields)
    {
        // Current Forever 69913 self-field serializer.
        //
        //   ObjectData        generated
        //   UnitData          generated
        //   PlayerData        generated
        //   ActivePlayerData  generated (conservative Forever profile)
        //   0x21 / 0x26       generated
        //
        // Unverified ActivePlayerData fields are emitted as their minimal
        // zero/empty representation. The old captured field payload is no longer used.

        ByteBuffer payload;

        payload << SELF_FIELD_FLAGS_69913;
        payload << FRAGMENT_CGOBJECT_69913;
        payload << FRAGMENT_PLAYER_HOUSE_INFO_69913;
        payload << FRAGMENT_PLAYER_INITIATIVE_69913;
        payload << FRAGMENT_TAG_UNIT_69913;
        payload << FRAGMENT_TAG_PLAYER_69913;
        payload << FRAGMENT_END_69913;

        // CGObject indirect fragment activation.
        payload << uint8_t(1);

        writeObjectDataCreate(payload, objectFields);
        writeUnitDataCreate(payload, unitFields, true);

        if (!writePlayerDataCreate(payload, playerFields, true))
            return {};

        // Forever ActivePlayerData now runs from our own conservative writer.
        // Only the capture-proven prefix and transmog-outfit island are populated
        // from live state. The unverified middle/tail fields are intentionally
        // serialized in their zero/empty state instead of borrowing semantics
        // from Midnight or copying the old retail capture.
        const Fields::ActivePlayerData conservativeActivePlayer =
            makeConservativeActivePlayerData69913(activePlayerFields);

        // Build the real live-state ActivePlayer block separately. This is the
        // block considered for the safety fallback below.
        ByteBuffer generatedActivePlayer;
        if (!writeActivePlayerDataCreateImpl(generatedActivePlayer, conservativeActivePlayer, nullptr))
            return {};

        // Do not splice the empty-inventory reference tail onto the live
        // generated prefix.  That reference belongs to a different captured
        // ActivePlayerData extent (39643 bytes versus 38749 bytes for the
        // known-good login capture), so the splice shifts the following
        // components and the client crashes while entering the world.
        //
        // Keep the known-good complete ActivePlayerData payload until the
        // second half of the 69913 create grammar has been isolated offline.
        auto const& activePlayerFallback = Template69913::ActivePlayerDataFallback;
        payload.append(activePlayerFallback.data(), activePlayerFallback.size());

        payload << uint8_t(1);
        writeEmptyPlayerHouseInfoComponentCreate69913(payload);

        payload << uint8_t(1);
        writeEmptyPlayerInitiativeComponentCreate69913(payload);

        return std::vector<uint8_t>(
            payload.contents(),
            payload.contents() + payload.size());
    }


    namespace
    {
    }


    std::vector<uint8_t> buildUpdateObjectPacket69913(uint16_t mapId, uint32_t updateCount, std::span<const uint8_t> updateBlocks)
    {
        if (updateCount == 0 || updateBlocks.empty())
            return {};

        ByteBuffer packet;
        packet << mapId << updateCount;
        packet.writeBit(1); // 69913 UpdateData leading bit
        packet.writeBit(0); // no destroy/out-of-range GUIDs
        packet.flushBits();
        packet << uint32_t(updateBlocks.size());
        packet.append(updateBlocks.data(), updateBlocks.size());
        return std::vector<uint8_t>(packet.contents(), packet.contents() + packet.size());
    }

    std::vector<uint8_t> buildUpdateObjectPacket(uint16_t mapId, std::span<const uint8_t> updateBlock)
    {
        return buildUpdateObjectPacket69913(mapId, 1, updateBlock);
    }
    std::vector<uint8_t> buildTemporary69913SelfCreatePacketWithFieldPayload(uint16_t mapId, std::span<const uint8_t> packedGuid, float x, float y, float z, float orientation, std::span<const uint8_t> fieldPayload)
    {
        if (packedGuid.empty() || fieldPayload.empty())
            return {};

        std::array<uint8_t, Template69913::MovementTail.size()> movementTail = Template69913::MovementTail;

        // The captured retail character was rooted. Do not inherit that captured
        // movement-control state for our generated player.
        uint32_t movementFlags = 0;
        std::memcpy(&movementFlags, movementTail.data(), sizeof(movementFlags));
        movementFlags &= ~uint32_t(0x00000400);
        std::memcpy(movementTail.data(), &movementFlags, sizeof(movementFlags));

        // 69913 carries the self position twice in this captured CreateObject2
        // movement block. Keep MovementInfo and EntityPosition synchronized.
        constexpr size_t movementPositionOffset = 12;
        constexpr size_t entityPositionOffset = 167;
        std::memcpy(movementTail.data() + movementPositionOffset + 0, &x, sizeof(float));
        std::memcpy(movementTail.data() + movementPositionOffset + 4, &y, sizeof(float));
        std::memcpy(movementTail.data() + movementPositionOffset + 8, &z, sizeof(float));
        std::memcpy(movementTail.data() + movementPositionOffset + 12, &orientation, sizeof(float));
        std::memcpy(movementTail.data() + entityPositionOffset + 0, &x, sizeof(float));
        std::memcpy(movementTail.data() + entityPositionOffset + 4, &y, sizeof(float));
        std::memcpy(movementTail.data() + entityPositionOffset + 8, &z, sizeof(float));

        ByteBuffer block;
        block << uint8_t(UPDATE_TYPE_CREATE_OBJECT_2);
        block.append(packedGuid.data(), packedGuid.size());
        block << uint8_t(OBJECT_TYPE_ACTIVE_PLAYER);
        block << uint8_t(0x8C) << uint8_t(0x00) << uint8_t(0x80);
        block << uint32_t(0);
        block.append(packedGuid.data(), packedGuid.size());
        block.append(movementTail.data(), movementTail.size());
        block << uint32_t(fieldPayload.size());
        block.append(fieldPayload.data(), fieldPayload.size());

        return buildUpdateObjectPacket(mapId, std::span<const uint8_t>(block.contents(), block.size()));
    }


}
