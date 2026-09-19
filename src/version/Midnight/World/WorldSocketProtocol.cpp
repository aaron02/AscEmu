/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

// Midnight WorldSocket adapter.
//
// All World V2/bootstrap/crypto glue lives here instead of the legacy
// world/Server/WorldSocket.cpp.  The generic WorldSocket exposes only neutral
// version hooks; this translation unit owns the Midnight-specific behaviour.

#include "world/Server/WorldSocket.hpp"

#include "world/Server/DatabaseDefinition.hpp"
#include "Logging/Logger.hpp"
#include "Utilities/Util.hpp"
#include "world/Management/ObjectMgr.hpp"
#include "world/Objects/Units/Players/PlayerDefines.hpp"
#include "version/Midnight/World/BattleNetComm/BattleNetCommClient.hpp"
#include "world/Server/OpcodeTable.hpp"
#include "world/Server/CharacterErrors.h"
#include "world/Server/World.h"
#include "world/Server/WorldSession.h"
#include "world/Storage/MySQLDataStore.hpp"
#include "version/Midnight/Auth.hpp"
#include "version/Midnight/BattleNet/Protocol.hpp"
#include "version/Midnight/BuildProfile.hpp"
#include "shared/WoWGuid.hpp"
#include "version/Midnight/OpcodeTable.hpp"
#include "version/Midnight/Opcodes.hpp"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace AscEmu::Packets;

namespace
{

    // Midnight character enumeration is now active. Keep the switch only as a
    // short-lived diagnostic escape hatch while the modern adapter is being stabilized.


    // ---------------------------------------------------------------------
    // Battle.net World V2 authentication packet model.
    //
    // Keep the packet layout in small structures/operators instead of
    // constructing SMSG_AUTH_RESPONSE inline in WorldSocket. The order below
    // uses the World V2 wire field order:
    //   AuthResponse -> AuthSuccessInfo -> GameTime -> VirtualRealmInfo.
    // ---------------------------------------------------------------------
    struct BNetVirtualRealmNameInfo
    {
        bool isLocal = false;
        bool isInternalRealm = false;
        std::string realmNameActual;
        std::string realmNameNormalized;
    };

    struct BNetVirtualRealmInfo
    {
        uint32_t realmAddress = 0;
        BNetVirtualRealmNameInfo realmNameInfo;
    };

    struct BNetGameTime
    {
        uint32_t billingType = 0;
        uint32_t minutesRemaining = 0;
        uint32_t realBillingType = 0;
        bool isInIGR = false;
        bool isPaidForByIGR = false;
        bool isCAISEnabled = false;
    };

    struct BNetClassAvailability
    {
        uint8_t classId = 0;
        uint8_t activeExpansionLevel = 0;
        uint8_t accountExpansionLevel = 0;
        uint8_t minActiveExpansionLevel = 0;
    };

    struct BNetRaceClassAvailability
    {
        uint8_t raceId = 0;
        std::vector<BNetClassAvailability> classes;
    };

    std::vector<BNetRaceClassAvailability> makeMidnightRaceClassAvailability()
    {
        // Advertise exactly the race/class combinations AscEmu can actually
        // create. playercreateinfo is already build-filtered by MySQLDataStore,
        // so this keeps the modern glue-screen entitlement data in sync with
        // the legacy character creation backend instead of hardcoding Midnight
        // combinations which the selected AscEmu expansion cannot spawn.
        std::vector<BNetRaceClassAvailability> result;
        result.reserve(DBC_NUM_RACES);

        for (uint8_t race = RACE_HUMAN; race < DBC_NUM_RACES; ++race)
        {
            BNetRaceClassAvailability availability;
            availability.raceId = race;

            for (uint8_t classId = WARRIOR; classId < MAX_PLAYER_CLASSES; ++classId)
            {
                if (sMySQLStore.getPlayerCreateInfo(race, classId) == nullptr)
                    continue;

                availability.classes.push_back({
                    classId,
                    0, // ActiveExpansionLevel: no additional Midnight entitlement required
                    0, // AccountExpansionLevel
                    0  // MinActiveExpansionLevel
                });
            }

            if (!availability.classes.empty())
                result.emplace_back(std::move(availability));
        }

        return result;
    }

    struct BNetAuthSuccessInfo
    {
        uint8_t activeExpansionLevel = 0;
        uint8_t accountExpansionLevel = 0;
        uint32_t timeRested = 0;
        uint32_t virtualRealmAddress = 0;
        uint32_t timeSecondsUntilPCKick = 0;
        uint32_t currencyId = 0;
        uint32_t time = 0;

        BNetGameTime gameTimeInfo;
        std::vector<BNetVirtualRealmInfo> virtualRealms;
        std::vector<BNetRaceClassAvailability> availableClasses;

        // Character templates are deliberately empty until AscEmu exposes
        // matching template data for the modern client. The wire format serializes a
        // uint32 count here even when the vector is empty.
        uint32_t templateCount = 0;

        bool isExpansionTrial = false;
        bool forceCharacterTemplate = false;

        bool hasNumPlayersHorde = false;
        uint16_t numPlayersHorde = 0;
        bool hasNumPlayersAlliance = false;
        uint16_t numPlayersAlliance = 0;
        bool hasExpansionTrialExpiration = false;
        int64_t expansionTrialExpiration = 0;
        bool hasCurrentBuild = false;
        std::array<uint8_t, 16> currentBuildKey{};
        std::array<uint8_t, 16> currentConfigKey{};
    };

    struct BNetAuthResponse
    {
        uint32_t result = 0;
        bool hasSuccessInfo = false;
        BNetAuthSuccessInfo successInfo;
        bool hasWaitInfo = false;
    };

    ByteBuffer& operator<<(ByteBuffer& data, const BNetVirtualRealmNameInfo& value)
    {
        data.writeBit(value.isLocal ? 1 : 0);
        data.writeBit(value.isInternalRealm ? 1 : 0);
        data.writeBits(static_cast<uint32_t>(value.realmNameActual.size()), 8);
        data.writeBits(static_cast<uint32_t>(value.realmNameNormalized.size()), 8);
        data.flushBits();
        data.append(reinterpret_cast<const uint8_t*>(value.realmNameActual.data()), value.realmNameActual.size());
        data.append(reinterpret_cast<const uint8_t*>(value.realmNameNormalized.data()), value.realmNameNormalized.size());
        return data;
    }

    ByteBuffer& operator<<(ByteBuffer& data, const BNetVirtualRealmInfo& value)
    {
        data << uint32_t(value.realmAddress);
        data << value.realmNameInfo;
        return data;
    }

    ByteBuffer& operator<<(ByteBuffer& data, const BNetGameTime& value)
    {
        data << uint32_t(value.billingType);
        data << uint32_t(value.minutesRemaining);
        data << uint32_t(value.realBillingType);
        data.writeBit(value.isInIGR ? 1 : 0);
        data.writeBit(value.isPaidForByIGR ? 1 : 0);
        data.writeBit(value.isCAISEnabled ? 1 : 0);
        data.flushBits();
        return data;
    }

    ByteBuffer& operator<<(ByteBuffer& data, const BNetAuthSuccessInfo& value)
    {
        // World V2 field order.
        data << uint32_t(value.virtualRealmAddress);
        data << uint32_t(static_cast<uint32_t>(value.virtualRealms.size()));
        data << uint32_t(value.timeRested);
        data << uint8_t(value.activeExpansionLevel);
        data << uint8_t(value.accountExpansionLevel);
        data << uint32_t(value.timeSecondsUntilPCKick);
        data << uint32_t(static_cast<uint32_t>(value.availableClasses.size()));
        data << uint32_t(value.templateCount);
        data << uint32_t(value.currencyId);

        // reference implementation 12.1.0.69814 wire order: GameTime and the 32-bit Timestamp
        // are part of the fixed AuthSuccessInfo prefix, before realm/class/template arrays.
        data << value.gameTimeInfo;
        data << uint32_t(value.time);

        for (const BNetVirtualRealmInfo& virtualRealm : value.virtualRealms)
            data << virtualRealm;

        for (const BNetRaceClassAvailability& raceAvailability : value.availableClasses)
        {
            data << uint8_t(raceAvailability.raceId);
            data << uint32_t(static_cast<uint32_t>(raceAvailability.classes.size()));
            for (const BNetClassAvailability& classAvailability : raceAvailability.classes)
            {
                data << uint8_t(classAvailability.classId);
                data << uint8_t(classAvailability.activeExpansionLevel);
                data << uint8_t(classAvailability.accountExpansionLevel);
                data << uint8_t(classAvailability.minActiveExpansionLevel);
            }
        }

        // Templates are currently empty, so no template records are emitted here.

        // reference implementation writes these presence/state bits only after all variable arrays.
        data.writeBit(value.isExpansionTrial ? 1 : 0);
        data.writeBit(value.forceCharacterTemplate ? 1 : 0);
        data.writeBit(value.hasNumPlayersHorde ? 1 : 0);
        data.writeBit(value.hasNumPlayersAlliance ? 1 : 0);
        data.writeBit(value.hasExpansionTrialExpiration ? 1 : 0);
        data.writeBit(value.hasCurrentBuild ? 1 : 0);
        data.flushBits();

        if (value.hasNumPlayersHorde)
            data << uint16_t(value.numPlayersHorde);
        if (value.hasNumPlayersAlliance)
            data << uint16_t(value.numPlayersAlliance);
        if (value.hasExpansionTrialExpiration)
            data << int64_t(value.expansionTrialExpiration);
        if (value.hasCurrentBuild)
        {
            for (size_t i = 0; i < value.currentBuildKey.size(); ++i)
            {
                data << uint8_t(value.currentBuildKey[i]);
                data << uint8_t(value.currentConfigKey[i]);
            }
        }

        return data;
    }

    void writeBNetAuthResponse(ByteBuffer& data, const BNetAuthResponse& value)
    {
        // World V2 AuthResponse prefix/presence-bit order.
        data << uint32_t(value.result);
        data.writeBit(value.hasSuccessInfo ? 1 : 0);
        data.writeBit(value.hasWaitInfo ? 1 : 0);
        data.flushBits();

        if (value.hasSuccessInfo)
            data << value.successInfo;

        // WaitInfo is not used on a successful immediate login. If queueing
        // is added later it must be serialized here with the verified wire layout.
    }

    bool verifyBattleNetV2ServerCiphertext(const std::vector<uint8_t>& ciphertext, const std::array<uint8_t, AscEmu::Version::Midnight::BattleNet::AuthTagSize>& tag, const std::array<uint8_t, 32>& key, uint64_t counter, const std::vector<uint8_t>& expectedPlaintext)
    {
        if (ciphertext.empty() || ciphertext.size() != expectedPlaintext.size())
            return false;

        std::array<uint8_t, 12> iv{};
        memcpy(iv.data(), &counter, sizeof(counter));
        memcpy(iv.data() + sizeof(counter), &AscEmu::Version::Midnight::BattleNet::ServerIvMagic, sizeof(AscEmu::Version::Midnight::BattleNet::ServerIvMagic));

        std::vector<uint8_t> plaintext = ciphertext;
        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        if (context == nullptr)
            return false;

        bool success = EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
        if (success)
            success = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) == 1;
        if (success)
            success = EVP_DecryptInit_ex(context, nullptr, nullptr, key.data(), iv.data()) == 1;

        int outputLength = 0;
        if (success)
            success = EVP_DecryptUpdate(context, plaintext.data(), &outputLength, plaintext.data(), static_cast<int>(plaintext.size())) == 1 &&
                outputLength == static_cast<int>(plaintext.size());

        std::array<uint8_t, AscEmu::Version::Midnight::BattleNet::AuthTagSize> mutableTag = tag;
        if (success)
            success = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, static_cast<int>(mutableTag.size()), mutableTag.data()) == 1;

        int finalLength = 0;
        if (success)
            success = EVP_DecryptFinal_ex(context, plaintext.data() + outputLength, &finalLength) == 1 && finalLength == 0;

        EVP_CIPHER_CTX_free(context);
        return success && plaintext == expectedPlaintext;
    }

    uint32_t readUInt32LE(const uint8_t* data)
    {
        uint32_t value = 0;
        memcpy(&value, data, sizeof(value));
        return value;
    }

    uint64_t readUInt64LE(const uint8_t* data)
    {
        uint64_t value = 0;
        memcpy(&value, data, sizeof(value));
        return value;
    }

    bool sha512(const uint8_t* first, size_t firstSize, const uint8_t* second, size_t secondSize, std::array<uint8_t, 64>& digest)
    {
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (context == nullptr)
            return false;

        bool success = EVP_DigestInit_ex(context, EVP_sha512(), nullptr) == 1;
        if (success && firstSize > 0)
            success = EVP_DigestUpdate(context, first, firstSize) == 1;
        if (success && secondSize > 0)
            success = EVP_DigestUpdate(context, second, secondSize) == 1;

        unsigned int digestSize = 0;
        if (success)
            success = EVP_DigestFinal_ex(context, digest.data(), &digestSize) == 1 && digestSize == digest.size();

        EVP_MD_CTX_free(context);
        return success;
    }

    bool calculateBattleNetAuthDigest(const std::array<uint8_t, 64>& worldAuthKeyData, const std::array<uint8_t, 16>* buildAuthKey, const std::array<uint8_t, 32>& localChallenge, const std::array<uint8_t, 32>& serverChallenge, std::array<uint8_t, 64>& result)
    {
        std::array<uint8_t, 64> digestKeyHash{};
        if (!sha512(worldAuthKeyData.data(), worldAuthKeyData.size(),
            buildAuthKey != nullptr ? buildAuthKey->data() : nullptr,
            buildAuthKey != nullptr ? buildAuthKey->size() : 0, digestKeyHash))
        {
            return false;
        }

        std::array<uint8_t, 96> hmacInput{};
        size_t offset = 0;
        memcpy(hmacInput.data() + offset, localChallenge.data(), localChallenge.size());
        offset += localChallenge.size();
        memcpy(hmacInput.data() + offset, serverChallenge.data(), serverChallenge.size());
        offset += serverChallenge.size();
        memcpy(hmacInput.data() + offset, AscEmu::Version::Midnight::AuthCheckSeed.data(),
            AscEmu::Version::Midnight::AuthCheckSeed.size());

        unsigned int digestSize = 0;
        const unsigned char* digest = HMAC(EVP_sha512(), digestKeyHash.data(), static_cast<int>(digestKeyHash.size()),
            hmacInput.data(), hmacInput.size(), result.data(), &digestSize);
        return digest != nullptr && digestSize == result.size();
    }

    bool digestMatches(const std::array<uint8_t, 64>& calculated, const std::array<uint8_t, 24>& received)
    {
        return CRYPTO_memcmp(calculated.data(), received.data(), received.size()) == 0;
    }

    std::string bytesToHex(const uint8_t* data, size_t size)
    {
        static constexpr char Hex[] = "0123456789ABCDEF";
        std::string result;
        result.resize(size * 2);
        for (size_t i = 0; i < size; ++i)
        {
            result[i * 2] = Hex[(data[i] >> 4) & 0x0F];
            result[i * 2 + 1] = Hex[data[i] & 0x0F];
        }
        return result;
    }

    bool sha512Parts(const std::vector<std::pair<const uint8_t*, size_t>>& parts, std::array<uint8_t, 64>& digest)
    {
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (context == nullptr)
            return false;

        bool success = EVP_DigestInit_ex(context, EVP_sha512(), nullptr) == 1;
        for (const auto& [data, size] : parts)
        {
            if (success && size > 0)
                success = EVP_DigestUpdate(context, data, size) == 1;
        }

        unsigned int digestSize = 0;
        if (success)
            success = EVP_DigestFinal_ex(context, digest.data(), &digestSize) == 1 && digestSize == digest.size();

        EVP_MD_CTX_free(context);
        return success;
    }

    bool hmacSha512(const uint8_t* key, size_t keySize, const std::vector<std::pair<const uint8_t*, size_t>>& parts, std::array<uint8_t, 64>& digest)
    {
        size_t totalSize = 0;
        for (const auto& part : parts)
            totalSize += part.second;

        std::vector<uint8_t> input;
        input.reserve(totalSize);
        for (const auto& [data, size] : parts)
        {
            if (size > 0)
                input.insert(input.end(), data, data + size);
        }

        unsigned int digestSize = 0;
        const unsigned char* result = HMAC(EVP_sha512(), key, static_cast<int>(keySize),
            input.empty() ? nullptr : input.data(), input.size(), digest.data(), &digestSize);
        return result != nullptr && digestSize == digest.size();
    }

    bool generateBattleNetSessionKey(const std::array<uint8_t, 64>& seed, std::array<uint8_t, 40>& sessionKey)
    {
        // Blizzard's SessionKeyGenerator hashes both halves into o1/o2, starts o0
        // at zero, then emits SHA512(o1 || o0 || o2) blocks.
        std::array<uint8_t, 64> o0{};
        std::array<uint8_t, 64> o1{};
        std::array<uint8_t, 64> o2{};

        if (!sha512Parts({ { seed.data(), seed.size() / 2 } }, o1) || !sha512Parts({ { seed.data() + seed.size() / 2, seed.size() / 2 } }, o2))
        {
            return false;
        }

        std::array<uint8_t, 64> block{};
        if (!sha512Parts({ { o1.data(), o1.size() }, { o0.data(), o0.size() }, { o2.data(), o2.size() } }, block))
            return false;

        memcpy(sessionKey.data(), block.data(), sessionKey.size());
        return true;
    }

    bool deriveBattleNetKeys(const std::array<uint8_t, 64>& worldAuthKeyData, const std::array<uint8_t, 32>& serverChallenge, const std::array<uint8_t, 32>& localChallenge, std::array<uint8_t, 40>& sessionKey, std::array<uint8_t, 32>& encryptionKey)
    {
        std::array<uint8_t, 64> keyDataDigest{};
        if (!sha512Parts({ { worldAuthKeyData.data(), worldAuthKeyData.size() } }, keyDataDigest))
            return false;

        std::array<uint8_t, 64> sessionSeed{};
        if (!hmacSha512(keyDataDigest.data(), keyDataDigest.size(),
            {
                { serverChallenge.data(), serverChallenge.size() },
                { localChallenge.data(), localChallenge.size() },
                { AscEmu::Version::Midnight::SessionKeySeed.data(), AscEmu::Version::Midnight::SessionKeySeed.size() }
            }, sessionSeed))
        {
            return false;
        }

        if (!generateBattleNetSessionKey(sessionSeed, sessionKey))
            return false;

        std::array<uint8_t, 64> encryptionDigest{};
        if (!hmacSha512(sessionKey.data(), sessionKey.size(),
            {
                { localChallenge.data(), localChallenge.size() },
                { serverChallenge.data(), serverChallenge.size() },
                { AscEmu::Version::Midnight::EncryptionKeySeed.data(), AscEmu::Version::Midnight::EncryptionKeySeed.size() }
            }, encryptionDigest))
        {
            return false;
        }

        memcpy(encryptionKey.data(), encryptionDigest.data(), encryptionKey.size());
        return true;
    }

    struct Ed25519Point
    {
        BIGNUM* x = BN_new();
        BIGNUM* y = BN_new();
        BIGNUM* z = BN_new();
        BIGNUM* t = BN_new();

        Ed25519Point() = default;
        Ed25519Point(const Ed25519Point&) = delete;
        Ed25519Point& operator=(const Ed25519Point&) = delete;

        ~Ed25519Point()
        {
            BN_free(x);
            BN_free(y);
            BN_free(z);
            BN_free(t);
        }

        bool isValid() const
        {
            return x != nullptr && y != nullptr && z != nullptr && t != nullptr;
        }
    };

    bool copyEd25519Point(Ed25519Point& destination, const Ed25519Point& source)
    {
        return BN_copy(destination.x, source.x) != nullptr &&
            BN_copy(destination.y, source.y) != nullptr &&
            BN_copy(destination.z, source.z) != nullptr &&
            BN_copy(destination.t, source.t) != nullptr;
    }

    BIGNUM* bnFromLittleEndian(const uint8_t* data, size_t size)
    {
        std::vector<uint8_t> bigEndian(data, data + size);
        std::reverse(bigEndian.begin(), bigEndian.end());
        return BN_bin2bn(bigEndian.data(), static_cast<int>(bigEndian.size()), nullptr);
    }

    bool bnToLittleEndian32(const BIGNUM* value, uint8_t* output)
    {
        std::array<uint8_t, 32> bigEndian{};
        if (BN_bn2binpad(value, bigEndian.data(), static_cast<int>(bigEndian.size())) != static_cast<int>(bigEndian.size()))
            return false;

        std::reverse_copy(bigEndian.begin(), bigEndian.end(), output);
        return true;
    }

    bool ed25519PointAdd(const Ed25519Point& left, const Ed25519Point& right,
        Ed25519Point& result, const BIGNUM* prime, const BIGNUM* twoD, BN_CTX* context)
    {
        BN_CTX_start(context);
        BIGNUM* a = BN_CTX_get(context);
        BIGNUM* b = BN_CTX_get(context);
        BIGNUM* c = BN_CTX_get(context);
        BIGNUM* d = BN_CTX_get(context);
        BIGNUM* e = BN_CTX_get(context);
        BIGNUM* f = BN_CTX_get(context);
        BIGNUM* g = BN_CTX_get(context);
        BIGNUM* h = BN_CTX_get(context);
        BIGNUM* temp1 = BN_CTX_get(context);
        BIGNUM* temp2 = BN_CTX_get(context);

        bool success = temp2 != nullptr;
        if (success)
            success = BN_mod_sub(temp1, left.y, left.x, prime, context) == 1 &&
                BN_mod_sub(temp2, right.y, right.x, prime, context) == 1 &&
                BN_mod_mul(a, temp1, temp2, prime, context) == 1;
        if (success)
            success = BN_mod_add(temp1, left.y, left.x, prime, context) == 1 &&
                BN_mod_add(temp2, right.y, right.x, prime, context) == 1 &&
                BN_mod_mul(b, temp1, temp2, prime, context) == 1;
        if (success)
            success = BN_mod_mul(temp1, left.t, right.t, prime, context) == 1 &&
                BN_mod_mul(c, temp1, twoD, prime, context) == 1;
        if (success)
            success = BN_mod_mul(temp1, left.z, right.z, prime, context) == 1 &&
                BN_mod_add(d, temp1, temp1, prime, context) == 1;
        if (success)
            success = BN_mod_sub(e, b, a, prime, context) == 1 &&
                BN_mod_sub(f, d, c, prime, context) == 1 &&
                BN_mod_add(g, d, c, prime, context) == 1 &&
                BN_mod_add(h, b, a, prime, context) == 1;
        if (success)
            success = BN_mod_mul(temp1, e, f, prime, context) == 1 && BN_copy(result.x, temp1) != nullptr;
        if (success)
            success = BN_mod_mul(temp1, g, h, prime, context) == 1 && BN_copy(result.y, temp1) != nullptr;
        if (success)
            success = BN_mod_mul(temp1, e, h, prime, context) == 1 && BN_copy(result.t, temp1) != nullptr;
        if (success)
            success = BN_mod_mul(temp1, f, g, prime, context) == 1 && BN_copy(result.z, temp1) != nullptr;

        BN_CTX_end(context);
        return success;
    }

    bool ed25519PointDouble(const Ed25519Point& point, Ed25519Point& result, const BIGNUM* prime, BN_CTX* context)
    {
        BN_CTX_start(context);
        BIGNUM* a = BN_CTX_get(context);
        BIGNUM* b = BN_CTX_get(context);
        BIGNUM* c = BN_CTX_get(context);
        BIGNUM* d = BN_CTX_get(context);
        BIGNUM* e = BN_CTX_get(context);
        BIGNUM* f = BN_CTX_get(context);
        BIGNUM* g = BN_CTX_get(context);
        BIGNUM* h = BN_CTX_get(context);
        BIGNUM* temp = BN_CTX_get(context);
        BIGNUM* zero = BN_CTX_get(context);

        bool success = zero != nullptr;
        if (success)
        {
            BN_zero(zero);
            success = BN_mod_sqr(a, point.x, prime, context) == 1 &&
                BN_mod_sqr(b, point.y, prime, context) == 1 &&
                BN_mod_sqr(temp, point.z, prime, context) == 1 &&
                BN_mod_add(c, temp, temp, prime, context) == 1 &&
                BN_mod_sub(d, zero, a, prime, context) == 1;
        }
        if (success)
            success = BN_mod_add(temp, point.x, point.y, prime, context) == 1 &&
                BN_mod_sqr(e, temp, prime, context) == 1 &&
                BN_mod_sub(e, e, a, prime, context) == 1 &&
                BN_mod_sub(e, e, b, prime, context) == 1;
        if (success)
            success = BN_mod_add(g, d, b, prime, context) == 1 &&
                BN_mod_sub(f, g, c, prime, context) == 1 &&
                BN_mod_sub(h, d, b, prime, context) == 1;
        if (success)
            success = BN_mod_mul(temp, e, f, prime, context) == 1 && BN_copy(result.x, temp) != nullptr;
        if (success)
            success = BN_mod_mul(temp, g, h, prime, context) == 1 && BN_copy(result.y, temp) != nullptr;
        if (success)
            success = BN_mod_mul(temp, e, h, prime, context) == 1 && BN_copy(result.t, temp) != nullptr;
        if (success)
            success = BN_mod_mul(temp, f, g, prime, context) == 1 && BN_copy(result.z, temp) != nullptr;

        BN_CTX_end(context);
        return success;
    }

    bool ed25519ScalarMultiplyBase(const BIGNUM* scalar, Ed25519Point& result, const BIGNUM* prime, const BIGNUM* twoD, BN_CTX* context)
    {
        Ed25519Point accumulator;
        Ed25519Point addend;
        Ed25519Point temporary;
        if (!accumulator.isValid() || !addend.isValid() || !temporary.isValid())
            return false;

        BN_zero(accumulator.x);
        BN_zero(accumulator.t);
        if (BN_one(accumulator.y) != 1 || BN_one(accumulator.z) != 1)
            return false;

        if (BN_dec2bn(&addend.x, "15112221349535400772501151409588531511454012693041857206046113283949847762202") == 0 ||
            BN_dec2bn(&addend.y, "46316835694926478169428394003475163141307993866256225615783033603165251855960") == 0 ||
            BN_one(addend.z) != 1 ||
            BN_mod_mul(addend.t, addend.x, addend.y, prime, context) != 1)
        {
            return false;
        }

        for (int bit = 0; bit < 256; ++bit)
        {
            if (BN_is_bit_set(scalar, bit) != 0)
            {
                if (!ed25519PointAdd(accumulator, addend, temporary, prime, twoD, context) ||
                    !copyEd25519Point(accumulator, temporary))
                {
                    return false;
                }
            }

            if (!ed25519PointDouble(addend, temporary, prime, context) ||
                !copyEd25519Point(addend, temporary))
            {
                return false;
            }
        }

        return copyEd25519Point(result, accumulator);
    }

    bool ed25519EncodePoint(const Ed25519Point& point, const BIGNUM* prime, BN_CTX* context, std::array<uint8_t, 32>& encoded)
    {
        BN_CTX_start(context);
        BIGNUM* inverseZ = BN_CTX_get(context);
        BIGNUM* x = BN_CTX_get(context);
        BIGNUM* y = BN_CTX_get(context);

        bool success = y != nullptr;
        if (success)
            success = BN_mod_inverse(inverseZ, point.z, prime, context) != nullptr &&
                BN_mod_mul(x, point.x, inverseZ, prime, context) == 1 &&
                BN_mod_mul(y, point.y, inverseZ, prime, context) == 1 &&
                bnToLittleEndian32(y, encoded.data());
        if (success && BN_is_odd(x) != 0)
            encoded.back() |= 0x80U;

        BN_CTX_end(context);
        return success;
    }

    bool ed25519ctxSign(const std::array<uint8_t, 32>& privateSeed, const uint8_t* message, size_t messageSize, const uint8_t* ed25519Context, size_t contextSize, std::array<uint8_t, 64>& signature)
    {
        if (contextSize == 0 || contextSize > 255)
            return false;

        BN_CTX* bnContext = BN_CTX_new();
        BIGNUM* prime = BN_new();
        BIGNUM* order = BN_new();
        BIGNUM* d = BN_new();
        BIGNUM* twoD = BN_new();
        BIGNUM* denominator = BN_new();
        BIGNUM* denominatorInverse = BN_new();
        BIGNUM* scalarA = nullptr;
        BIGNUM* scalarR = nullptr;
        BIGNUM* scalarK = nullptr;
        BIGNUM* scalarS = BN_new();

        bool success = bnContext != nullptr && prime != nullptr && order != nullptr && d != nullptr &&
            twoD != nullptr && denominator != nullptr && denominatorInverse != nullptr && scalarS != nullptr;

        if (success)
        {
            success = BN_set_bit(prime, 255) == 1 && BN_sub_word(prime, 19) == 1 &&
                BN_set_bit(order, 252) == 1;
        }

        BIGNUM* orderTail = nullptr;
        if (success)
        {
            success = BN_dec2bn(&orderTail, "27742317777372353535851937790883648493") != 0 &&
                BN_add(order, order, orderTail) == 1;
        }

        // d = -121665 / 121666 mod p
        if (success)
        {
            success = BN_set_word(denominator, 121666) == 1 &&
                BN_mod_inverse(denominatorInverse, denominator, prime, bnContext) != nullptr &&
                BN_set_word(d, 121665) == 1 &&
                BN_mod_mul(d, d, denominatorInverse, prime, bnContext) == 1 &&
                BN_sub(d, prime, d) == 1 &&
                BN_mod_add(twoD, d, d, prime, bnContext) == 1;
        }

        std::array<uint8_t, 64> privateDigest{};
        if (success)
            success = sha512Parts({ { privateSeed.data(), privateSeed.size() } }, privateDigest);

        std::array<uint8_t, 32> scalarABytes{};
        if (success)
        {
            memcpy(scalarABytes.data(), privateDigest.data(), scalarABytes.size());
            scalarABytes[0] &= 248U;
            scalarABytes[31] &= 63U;
            scalarABytes[31] |= 64U;
            scalarA = bnFromLittleEndian(scalarABytes.data(), scalarABytes.size());
            success = scalarA != nullptr;
        }

        Ed25519Point publicPoint;
        std::array<uint8_t, 32> publicKey{};
        if (success)
            success = publicPoint.isValid() &&
                ed25519ScalarMultiplyBase(scalarA, publicPoint, prime, twoD, bnContext) &&
                ed25519EncodePoint(publicPoint, prime, bnContext, publicKey);

        constexpr std::array<uint8_t, 32> dom2Prefix =
        {
            'S','i','g','E','d','2','5','5','1','9',' ','n','o',' ','E','d',
            '2','5','5','1','9',' ','c','o','l','l','i','s','i','o','n','s'
        };
        const uint8_t dom2Flag = 0;
        const uint8_t contextLength = static_cast<uint8_t>(contextSize);

        std::array<uint8_t, 64> rDigest{};
        if (success)
        {
            success = sha512Parts({
                { dom2Prefix.data(), dom2Prefix.size() },
                { &dom2Flag, sizeof(dom2Flag) },
                { &contextLength, sizeof(contextLength) },
                { ed25519Context, contextSize },
                { privateDigest.data() + 32, 32 },
                { message, messageSize }
            }, rDigest);
        }

        if (success)
        {
            scalarR = bnFromLittleEndian(rDigest.data(), rDigest.size());
            success = scalarR != nullptr && BN_mod(scalarR, scalarR, order, bnContext) == 1;
        }

        Ed25519Point rPoint;
        std::array<uint8_t, 32> encodedR{};
        if (success)
            success = rPoint.isValid() &&
                ed25519ScalarMultiplyBase(scalarR, rPoint, prime, twoD, bnContext) &&
                ed25519EncodePoint(rPoint, prime, bnContext, encodedR);

        std::array<uint8_t, 64> kDigest{};
        if (success)
        {
            success = sha512Parts({
                { dom2Prefix.data(), dom2Prefix.size() },
                { &dom2Flag, sizeof(dom2Flag) },
                { &contextLength, sizeof(contextLength) },
                { ed25519Context, contextSize },
                { encodedR.data(), encodedR.size() },
                { publicKey.data(), publicKey.size() },
                { message, messageSize }
            }, kDigest);
        }

        if (success)
        {
            scalarK = bnFromLittleEndian(kDigest.data(), kDigest.size());
            success = scalarK != nullptr && BN_mod(scalarK, scalarK, order, bnContext) == 1 &&
                BN_mod_mul(scalarS, scalarK, scalarA, order, bnContext) == 1 &&
                BN_mod_add(scalarS, scalarS, scalarR, order, bnContext) == 1;
        }

        if (success)
        {
            memcpy(signature.data(), encodedR.data(), encodedR.size());
            success = bnToLittleEndian32(scalarS, signature.data() + encodedR.size());
        }

        BN_clear_free(scalarA);
        BN_clear_free(scalarR);
        BN_clear_free(scalarK);
        BN_free(orderTail);
        BN_free(scalarS);
        BN_free(denominatorInverse);
        BN_free(denominator);
        BN_free(twoD);
        BN_free(d);
        BN_free(order);
        BN_free(prime);
        BN_CTX_free(bnContext);
        return success;
    }



    void appendMidnightObjectGuid(ByteBuffer& buffer, const WoWGuid& guid)
    {
        const std::vector<uint8_t> packed = guid.packModern();
        buffer.append(packed.data(), packed.size());
    }


    bool createEnterEncryptedModeSignature(const std::array<uint8_t, 32>& encryptionKey, bool enabled, std::array<uint8_t, 64>& signature)
    {
        const uint8_t enabledByte = enabled ? 1U : 0U;
        std::array<uint8_t, 64> toSign{};
        if (!hmacSha512(encryptionKey.data(), encryptionKey.size(),
            {
                { &enabledByte, sizeof(enabledByte) },
                { AscEmu::Version::Midnight::EnableEncryptionSeed.data(), AscEmu::Version::Midnight::EnableEncryptionSeed.size() }
            }, toSign))
        {
            return false;
        }

        // AscEmu currently ships OpenSSL 3.0, whose Ed25519 EVP implementation
        // only supports plain Ed25519 and silently ignores the Ed25519ctx
        // parameters used by newer OpenSSL versions. Implement RFC 8032
        // Ed25519ctx explicitly so the WoW client verifies the signature with
        // its embedded EnterEncryptedMode public key.
        return ed25519ctxSign(
            AscEmu::Version::Midnight::EnableEncryptionPrivateKey,
            toSign.data(), toSign.size(),
            AscEmu::Version::Midnight::EnableEncryptionContext.data(),
            AscEmu::Version::Midnight::EnableEncryptionContext.size(),
            signature);
    }

}

// ---------------------------------------------------------------------------
// Neutral hooks called by legacy WorldSocket.cpp.
// ---------------------------------------------------------------------------
bool WorldSocket::initializeVersionedConnection()
{
    if (!worldConfig.battleNetWorld.enabled)
        return false;

    m_protocolSetByLogonComm = false;

    WoW::ClientProtocol protocol;
    protocol.expansion = WoW::Expansion::Unknown;
    setClientProtocol(protocol);

    m_battleNetV2State = BattleNetV2State::AwaitClientInitializer;

    sLogger.info("WorldSocket::Midnight: World V2 connection from {}:{}; bypassing legacy LogonComm build lookup.",
        getRemoteIp(), getRemotePort());

    if (!sendBattleNetV2ServerInitializer())
    {
        sLogger.failure("WorldSocket::Midnight: failed to send World V2 server initializer to {}:{}.",
            getRemoteIp(), getRemotePort());
        disconnect();
    }

    return true;
}

bool WorldSocket::processVersionedRead()
{
    if (m_battleNetV2State == BattleNetV2State::Disabled)
        return false;

    if (m_battleNetV2State == BattleNetV2State::AwaitClientInitializer)
    {
        if (!processBattleNetV2Initializer())
            return true;
    }

    if (m_battleNetV2State == BattleNetV2State::AwaitAuthSession ||
        m_battleNetV2State == BattleNetV2State::AuthSessionObserved ||
        m_battleNetV2State == BattleNetV2State::AwaitEncryptionAck)
    {
        while (m_battleNetV2State != BattleNetV2State::Encrypted && processBattleNetV2Packet())
        {
        }

        if (m_battleNetV2State != BattleNetV2State::Encrypted)
            return true;
    }

    if (m_battleNetV2State == BattleNetV2State::Encrypted)
    {
        while (processBattleNetV2EncryptedPacket())
        {
        }

        return true;
    }

    return true;
}

bool WorldSocket::setVersionedClientProtocolByBuild(uint32_t build)
{
    if (build != AscEmu::Version::Midnight::Build)
        return false;

    WoW::ClientProtocol protocol;
    protocol.expansion = WoW::Expansion::MN;
    setClientProtocol(protocol);
    return true;
}

bool WorldSocket::sendVersionedPacket(WorldPacket* packet)
{
    if (m_battleNetV2State != BattleNetV2State::Encrypted)
        return false;

    // A legacy WorldPacket payload is not valid on Midnight's World V2 wire
    // format. Every server packet must have an explicit modern serializer.
    return true;
}

bool WorldSocket::sendBattleNetV2ServerInitializer()
{
    if (!isConnected())
        return false;

    burstBegin();
    const bool sent = burstSend(reinterpret_cast<const uint8_t*>(AscEmu::Version::Midnight::BattleNet::WorldServerInitializer.data()),
        static_cast<uint32_t>(AscEmu::Version::Midnight::BattleNet::WorldServerInitializer.size()));
    if (sent)
        burstPush();
    burstEnd();

    if (sent)
    {
    }

    return sent;
}

bool WorldSocket::processBattleNetV2Initializer()
{
    if (readBuffer.GetSize() < AscEmu::Version::Midnight::BattleNet::WorldClientInitializer.size())
        return false;

    std::array<char, 64> initializer{};
    static_assert(AscEmu::Version::Midnight::BattleNet::WorldClientInitializer.size() < initializer.size());
    readBuffer.Read(reinterpret_cast<uint8_t*>(initializer.data()), AscEmu::Version::Midnight::BattleNet::WorldClientInitializer.size());

    const std::string_view received(initializer.data(), AscEmu::Version::Midnight::BattleNet::WorldClientInitializer.size());
    if (received != AscEmu::Version::Midnight::BattleNet::WorldClientInitializer)
    {
        sLogger.failure("WorldSocket::BattleNetV2: invalid client initializer from {}:{}; closing socket.",
            getRemoteIp(), getRemotePort());
        disconnect();
        return false;
    }

    m_battleNetV2State = BattleNetV2State::AwaitAuthSession;
    sLogger.info("WorldSocket::BattleNetV2: client initializer accepted from {}:{}.", getRemoteIp(), getRemotePort());

    if (!sendBattleNetV2AuthChallenge())
    {
        sLogger.failure("WorldSocket::BattleNetV2: failed to send modern auth challenge to {}:{}.", getRemoteIp(), getRemotePort());
        disconnect();
        return false;
    }

    return true;
}

bool WorldSocket::sendBattleNetV2Packet(uint32_t opcode, const uint8_t* payload, uint32_t payloadSize)
{
    if (!isConnected() || opcode == 0 || payloadSize > AscEmu::Version::Midnight::BattleNet::MaxPacketSize - sizeof(uint32_t))
        return false;

    const uint32_t packetSize = static_cast<uint32_t>(sizeof(uint32_t)) + payloadSize;
    std::array<uint8_t, AscEmu::Version::Midnight::BattleNet::ServerHeaderSize> header{};
    memcpy(header.data(), &packetSize, sizeof(packetSize));

    if (m_battleNetV2State == BattleNetV2State::Encrypted)
    {
        std::vector<uint8_t> encrypted(packetSize);
        memcpy(encrypted.data(), &opcode, sizeof(opcode));
        if (payloadSize > 0)
            memcpy(encrypted.data() + sizeof(opcode), payload, payloadSize);

        std::array<uint8_t, AscEmu::Version::Midnight::BattleNet::AuthTagSize> tag{};
        if (!encryptBattleNetV2Payload(encrypted, tag))
        {
            sLogger.failure("WorldSocket::BattleNetV2: AES-256-GCM encryption failed for opcode=0x{:08X}, crypto_send_counter={}.",
                opcode, m_battleNetV2CryptoSendCounter);
            return false;
        }

        memcpy(header.data() + sizeof(packetSize), tag.data(), tag.size());

        burstBegin();
        bool sent = burstSend(header.data(), static_cast<uint32_t>(header.size()));
        if (sent)
            sent = burstSend(encrypted.data(), static_cast<uint32_t>(encrypted.size()));
        if (sent)
            burstPush();
        burstEnd();

        if (sent)
        {
            ++m_battleNetV2SendCounter;
        }

        return sent;
    }

    burstBegin();
    bool sent = burstSend(header.data(), static_cast<uint32_t>(header.size()));
    if (sent)
        sent = burstSend(reinterpret_cast<const uint8_t*>(&opcode), static_cast<uint32_t>(sizeof(opcode)));
    if (sent && payloadSize > 0)
        sent = burstSend(payload, payloadSize);
    if (sent)
        burstPush();
    burstEnd();

    if (sent)
        ++m_battleNetV2SendCounter;

    return sent;
}

bool WorldSocket::sendBattleNetV2AuthChallenge()
{
    if (RAND_bytes(m_battleNetServerChallenge.data(), static_cast<int>(m_battleNetServerChallenge.size())) != 1 || RAND_bytes(m_battleNetDosChallenge.data(), static_cast<int>(m_battleNetDosChallenge.size())) != 1)
    {
        sLogger.failure("WorldSocket::BattleNetV2: RAND_bytes failed while creating the auth challenge.");
        return false;
    }

    std::array<uint8_t, AscEmu::Version::Midnight::BattleNet::AuthChallengePayloadSize> payload{};
    memcpy(payload.data(), m_battleNetDosChallenge.data(), m_battleNetDosChallenge.size());
    memcpy(payload.data() + m_battleNetDosChallenge.size(), m_battleNetServerChallenge.data(), m_battleNetServerChallenge.size());
    payload.back() = 1; // DosZeroBits

    const uint32_t opcode = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_AUTH_CHALLENGE);
    if (!sendBattleNetV2Packet(opcode, payload.data(), static_cast<uint32_t>(payload.size())))
        return false;
    return true;
}

bool WorldSocket::processBattleNetV2Packet()
{
    if (m_battleNetV2PacketRemaining == 0)
    {
        if (readBuffer.GetSize() < AscEmu::Version::Midnight::BattleNet::ClientHeaderSize)
            return false;

        std::array<uint8_t, AscEmu::Version::Midnight::BattleNet::ClientHeaderSize> header{};
        if (!readBuffer.Read(header.data(), header.size()))
            return false;

        const uint32_t packetSize = readUInt32LE(header.data());
        if (packetSize < sizeof(uint32_t) || packetSize > AscEmu::Version::Midnight::BattleNet::MaxPacketSize)
        {
            sLogger.failure("WorldSocket::BattleNetV2: invalid packet size {} from {}:{}; closing socket.", packetSize, getRemoteIp(), getRemotePort());
            disconnect();
            return false;
        }

        bool hasNonZeroTag = false;
        for (uint32_t i = 4; i < AscEmu::Version::Midnight::BattleNet::ServerHeaderSize; ++i)
            hasNonZeroTag = hasNonZeroTag || header[i] != 0;

        m_battleNetV2PacketOpcode = readUInt32LE(header.data() + AscEmu::Version::Midnight::BattleNet::ServerHeaderSize);
        m_battleNetV2PacketRemaining = packetSize - static_cast<uint32_t>(sizeof(uint32_t));
    }

    if (readBuffer.GetSize() < m_battleNetV2PacketRemaining)
        return false;

    const uint32_t opcode = m_battleNetV2PacketOpcode;
    const uint32_t payloadSize = m_battleNetV2PacketRemaining;
    std::vector<uint8_t> payload(payloadSize);
    if (payloadSize > 0 && !readBuffer.Read(payload.data(), payloadSize))
        return false;

    m_battleNetV2PacketOpcode = 0;
    m_battleNetV2PacketRemaining = 0;
    ++m_battleNetV2RecvCounter;

    if (opcode == AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::CMSG_LOG_DISCONNECT))
    {
        if (payload.size() != sizeof(uint32_t))
        {
            sLogger.failure("WorldSocket::BattleNetV2: malformed CMSG_LOG_DISCONNECT while state={} build={}; expected 4 payload bytes, got {} (payload_hex={}).", static_cast<uint32_t>(m_battleNetV2State), m_battleNetClientBuild, payload.size(), bytesToHex(payload.data(), payload.size()));
        }
        else
        {
            const uint32_t reason = readUInt32LE(payload.data());
            sLogger.info("WorldSocket::BattleNetV2: client sent CMSG_LOG_DISCONNECT reason={} while state={} build={} from {}:{}.", reason, static_cast<uint32_t>(m_battleNetV2State), m_battleNetClientBuild, getRemoteIp(), getRemotePort());
        }

        disconnect();
        return false;
    }

    if (opcode == AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::CMSG_AUTH_SESSION))
        return processBattleNetV2AuthSession(opcode, payload);

    if (m_battleNetV2State == BattleNetV2State::AwaitEncryptionAck)
    {
        const uint32_t expectedAckOpcode = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::CMSG_ENTER_ENCRYPTED_MODE_ACK);
        if (opcode == expectedAckOpcode)
            return processBattleNetV2EnterEncryptedModeAck(opcode, payload);

        sLogger.info("WorldSocket::BattleNetV2: unexpected opcode 0x{:08X} while awaiting CMSG_ENTER_ENCRYPTED_MODE_ACK; build={} payload_bytes={} payload_hex={}; ignoring packet.", opcode, m_battleNetClientBuild, payload.size(), bytesToHex(payload.data(), payload.size()));
        return true;
    }

    sLogger.info("WorldSocket::BattleNetV2: unhandled auth opcode 0x{:08X} while state={} build={} payload_bytes={} payload_hex={}; ignoring packet.", opcode, static_cast<uint32_t>(m_battleNetV2State), m_battleNetClientBuild, payload.size(), bytesToHex(payload.data(), payload.size()));
    return true;
}

bool WorldSocket::processBattleNetV2AuthSession(uint32_t opcode, const std::vector<uint8_t>& payload)
{
    if (payload.size() < AscEmu::Version::Midnight::BattleNet::AuthSessionFixedSize + sizeof(uint32_t))
    {
        sLogger.failure("WorldSocket::BattleNetV2: malformed CMSG_AUTH_SESSION 0x{:08X}; only {} byte(s).", opcode, payload.size());
        disconnect();
        return false;
    }

    size_t offset = 0;
    const uint64_t dosResponse = readUInt64LE(payload.data() + offset);
    offset += sizeof(uint64_t);
    const uint32_t regionId = readUInt32LE(payload.data() + offset);
    offset += sizeof(uint32_t);
    const uint32_t battlegroupId = readUInt32LE(payload.data() + offset);
    offset += sizeof(uint32_t);
    const uint32_t realmId = readUInt32LE(payload.data() + offset);
    offset += sizeof(uint32_t);

    std::array<uint8_t, 32> localChallenge{};
    memcpy(localChallenge.data(), payload.data() + offset, localChallenge.size());
    offset += localChallenge.size();

    std::array<uint8_t, 24> digest{};
    memcpy(digest.data(), payload.data() + offset, digest.size());
    offset += digest.size();
    const bool useIPv6 = (payload[offset] & 0x01U) != 0;
    ++offset;

    const uint32_t ticketSize = readUInt32LE(payload.data() + offset);
    offset += sizeof(uint32_t);
    if (ticketSize == 0 || ticketSize > payload.size() - offset)
    {
        sLogger.failure("WorldSocket::BattleNetV2: malformed CMSG_AUTH_SESSION ticket size {} (remaining {}).", ticketSize, payload.size() - offset);
        disconnect();
        return false;
    }

    const std::string realmJoinTicket(reinterpret_cast<const char*>(payload.data() + offset), ticketSize);
    offset += ticketSize;

    AscEmu::BattlenetComm::PendingWorldSession pendingSession;
    const bool pendingFound = AscEmu::BattlenetComm::sBattleNetCommClient.getPendingSession(realmJoinTicket, pendingSession, false);

    if (!pendingFound)
    {
        sLogger.failure("WorldSocket::BattleNetV2: RealmJoinTicket has no pending BattleNetComm session; rejecting pre-auth packet.");
        disconnect();
        return false;
    }

    // The authenticated RealmJoin build is authoritative for Battle.net V2.
    // Never fall back to world.conf ClientVersion for this connection.
    setClientProtocolByBuild(pendingSession.clientBuild);
    if (getClientProtocol().expansion == WoW::Expansion::Unknown)
    {
        sLogger.failure("WorldSocket::BattleNetV2: unsupported authenticated client build {}; refusing to use world.conf ClientVersion as fallback.", pendingSession.clientBuild);
        disconnect();
        return false;
    }

    if (pendingSession.realmId != realmId || pendingSession.region != regionId)
    {
        sLogger.failure("WorldSocket::BattleNetV2: pending-session scope mismatch: packet region/realm={}/{}, pending={}/{}.", regionId, realmId, pendingSession.region, pendingSession.realmId);
        disconnect();
        return false;
    }

    const auto buildAuthKey = AscEmu::Version::Midnight::getBuildAuthKey(pendingSession.clientBuild);
    if (!buildAuthKey.has_value())
    {
        // Deliberately keep this diagnostic for future client builds. It contains
        // per-login secret material and is emitted only when the server has no
        // verified build key, never during a normal login of a known build.
        sLogger.info("WorldSocket::BattleNetV2: missing-build-key build={} key_data={} local_challenge={} server_challenge={} client_digest={}", pendingSession.clientBuild, bytesToHex(pendingSession.worldAuthKeyData.data(), pendingSession.worldAuthKeyData.size()), bytesToHex(localChallenge.data(), localChallenge.size()), bytesToHex(m_battleNetServerChallenge.data(), m_battleNetServerChallenge.size()), bytesToHex(digest.data(), digest.size()));
        sLogger.failure("WorldSocket::BattleNetV2: no verified build auth key for build {}; rejecting login.", pendingSession.clientBuild);
        disconnect();
        return false;
    }

    std::array<uint8_t, 64> calculatedDigest{};
    if (!calculateBattleNetAuthDigest(pendingSession.worldAuthKeyData, &buildAuthKey.value(), localChallenge, m_battleNetServerChallenge, calculatedDigest))
    {
        sLogger.failure("WorldSocket::BattleNetV2: failed to calculate auth digest for build {}.", pendingSession.clientBuild);
        disconnect();
        return false;
    }

    if (!digestMatches(calculatedDigest, digest))
    {
        sLogger.failure("WorldSocket::BattleNetV2: CMSG_AUTH_SESSION digest mismatch for account={} build={}.", pendingSession.accountId, pendingSession.clientBuild);
        disconnect();
        return false;
    }

    AscEmu::BattlenetComm::PendingWorldSession consumedSession;
    if (!AscEmu::BattlenetComm::sBattleNetCommClient.getPendingSession(realmJoinTicket, consumedSession, true))
    {
        sLogger.failure("WorldSocket::BattleNetV2: pending session disappeared before it could be consumed.");
        disconnect();
        return false;
    }

    // Keep the authenticated identity after the one-shot RealmJoinTicket is
    // consumed. external reference keeps the WorldSession around during the encryption
    // handshake and publishes it only after CMSG_ENTER_ENCRYPTED_MODE_ACK.
    m_battleNetAccountId = consumedSession.accountId;
    m_battleNetGameAccountId = consumedSession.gameAccountId;
    m_battleNetRealmId = consumedSession.realmId;
    m_battleNetRegionId = consumedSession.region;
    m_battleNetGameAccountName = consumedSession.gameAccountName;
    m_battleNetClientBuild = consumedSession.clientBuild;
    m_clientBuild = consumedSession.clientBuild;
    m_accountName = consumedSession.gameAccountName;

    m_battleNetV2State = BattleNetV2State::AuthDigestVerified;

    if (!deriveBattleNetKeys(pendingSession.worldAuthKeyData, m_battleNetServerChallenge, localChallenge, m_battleNetSessionKey, m_battleNetEncryptionKey))
    {
        sLogger.failure("WorldSocket::BattleNetV2: failed to derive session/encryption keys for account={} build={}.", pendingSession.accountId, pendingSession.clientBuild);
        disconnect();
        return false;
    }

    if (!sendBattleNetV2EnterEncryptedMode(regionId))
    {
        sLogger.failure("WorldSocket::BattleNetV2: failed to send SMSG_ENTER_ENCRYPTED_MODE for account={} build={}.", pendingSession.accountId, pendingSession.clientBuild);
        disconnect();
        return false;
    }

    m_battleNetV2State = BattleNetV2State::AwaitEncryptionAck;
    return true;
}

bool WorldSocket::sendBattleNetV2EnterEncryptedMode(uint32_t regionGroup)
{
    // Midnight carries an explicit int32 RegionGroup before the
    // Ed25519ctx signature. Midnight protocol reference leaves this field at its protocol
    // default (0) for the normal realm connection; do not mirror RegionID
    // into it. Keep the argument for the existing WorldSocket interface.
    (void)regionGroup;
    constexpr int32_t wireRegionGroup = 0;

    if (m_battleNetClientBuild != AscEmu::Version::Midnight::Build)
    {
        sLogger.failure("WorldSocket::BattleNetV2: unsupported World V2 build {} while sending SMSG_ENTER_ENCRYPTED_MODE.", m_battleNetClientBuild);
        return false;
    }

    std::array<uint8_t, 64> signature{};
    if (!createEnterEncryptedModeSignature(m_battleNetEncryptionKey, true, signature))
    {
        sLogger.failure("WorldSocket::BattleNetV2: Ed25519ctx signing failed while building SMSG_ENTER_ENCRYPTED_MODE.");
        return false;
    }

    // wire layout (Midnight protocol reference):
    //   int32 RegionGroup
    //   uint8 Signature[64]
    //   bit Enabled (flushed to one byte, MSB-first in the WoW bit writer)
    // Total: 4 + 64 + 1 = 69 bytes.
    std::array<uint8_t, 69> payload{};
    const uint32_t regionGroupWire = static_cast<uint32_t>(wireRegionGroup);
    payload[0] = static_cast<uint8_t>(regionGroupWire & 0xFFu);
    payload[1] = static_cast<uint8_t>((regionGroupWire >> 8u) & 0xFFu);
    payload[2] = static_cast<uint8_t>((regionGroupWire >> 16u) & 0xFFu);
    payload[3] = static_cast<uint8_t>((regionGroupWire >> 24u) & 0xFFu);
    memcpy(payload.data() + sizeof(uint32_t), signature.data(), signature.size());
    payload.back() = 0x80; // Enabled = true

    const uint32_t opcode = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_ENTER_ENCRYPTED_MODE);
    if (!sendBattleNetV2Packet(opcode, payload.data(), static_cast<uint32_t>(payload.size())))
        return false;
    return true;
}

bool WorldSocket::processBattleNetV2EnterEncryptedModeAck(uint32_t opcode, const std::vector<uint8_t>& payload)
{
    if (m_battleNetV2State != BattleNetV2State::AwaitEncryptionAck)
    {
        sLogger.failure("WorldSocket::BattleNetV2: unexpected encrypted-mode ACK 0x{:08X} while state={} build={}.", opcode, static_cast<uint32_t>(m_battleNetV2State), m_battleNetClientBuild);
        disconnect();
        return false;
    }

    const uint32_t expectedOpcode = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::CMSG_ENTER_ENCRYPTED_MODE_ACK);
    if (opcode != expectedOpcode)
    {
        sLogger.failure("WorldSocket::BattleNetV2: wrong encrypted-mode response opcode 0x{:08X} for build {}; expected success ACK 0x{:08X}.", opcode, m_battleNetClientBuild, expectedOpcode);
        disconnect();
        return false;
    }

    if (!payload.empty())
    {
        sLogger.failure("WorldSocket::BattleNetV2: malformed encrypted-mode success ACK 0x{:08X} for build {}; expected empty payload, got {} byte(s).", opcode, m_battleNetClientBuild, payload.size());
        disconnect();
        return false;
    }

    // Enable AES-GCM only after the expected empty acknowledgement.
    m_battleNetV2CryptoSendCounter = m_battleNetV2SendCounter;
    m_battleNetV2CryptoRecvCounter = m_battleNetV2RecvCounter;
    m_battleNetV2EncryptedHeaderReady = false;
    m_battleNetV2EncryptedPacketSize = 0;
    m_battleNetV2EncryptedPacketTag.fill(0);
    m_battleNetV2EncryptedOpcode.fill(0);
    m_battleNetV2State = BattleNetV2State::Encrypted;

    sLogger.info("WorldSocket::BattleNetV2: encrypted-mode ACK accepted opcode=0x{:08X}; AES-256-GCM enabled (crypto_send_counter={}, crypto_recv_counter={}, transport_send_counter={}, transport_recv_counter={}).", opcode, m_battleNetV2CryptoSendCounter, m_battleNetV2CryptoRecvCounter, m_battleNetV2SendCounter, m_battleNetV2RecvCounter);

    if (!finalizeBattleNetV2WorldSession())
    {
        disconnect();
        return false;
    }

    return true;
}

bool WorldSocket::sendBattleNetV2AuthResponse()
{
    constexpr uint32_t ERROR_OK = 0;
    constexpr uint8_t ACCOUNT_EXPANSION_LEVEL = 11; // Midnight / authenticated protocol

    const uint32_t virtualRealmAddress =
        ((m_battleNetRegionId & 0xFFU) << 24U) |
        (1U << 16U) |
        (m_battleNetRealmId & 0xFFFFU);

    std::string realmName = sLogonCommHandler.getRealmName(m_battleNetRealmId);
    if (realmName.empty())
        realmName = "AscEmu";

    std::string normalizedRealmName;
    normalizedRealmName.reserve(realmName.size());
    for (const char character : realmName)
    {
        if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
            normalizedRealmName.push_back(character);
    }
    if (normalizedRealmName.empty())
        normalizedRealmName = realmName;

    // VirtualRealmNameInfo has two 8-bit string lengths.
    if (realmName.size() > 0xFFU)
        realmName.resize(0xFFU);
    if (normalizedRealmName.size() > 0xFFU)
        normalizedRealmName.resize(0xFFU);

    BNetAuthResponse response;
    response.result = ERROR_OK;
    response.hasSuccessInfo = true;
    response.hasWaitInfo = false;

    BNetAuthSuccessInfo& success = response.successInfo;
    success.virtualRealmAddress = virtualRealmAddress;
    success.timeRested = 0;
    success.activeExpansionLevel = ACCOUNT_EXPANSION_LEVEL;
    success.accountExpansionLevel = ACCOUNT_EXPANSION_LEVEL;
    success.timeSecondsUntilPCKick = 0;
    success.currencyId = 0;
    success.time = static_cast<uint32_t>(UNIXTIME);
    success.templateCount = 0;
    success.isExpansionTrial = false;
    success.forceCharacterTemplate = false;

    success.virtualRealms.push_back(
        {
            virtualRealmAddress, { true, false, realmName, normalizedRealmName }
        });

    // Build modern entitlement data from AscEmu's loaded playercreateinfo.
    // The same availability set is used by SMSG_AUTH_RESPONSE and character enum.
    success.availableClasses = makeMidnightRaceClassAvailability();

    ByteBuffer generatedPayload(768);
    writeBNetAuthResponse(generatedPayload, response);

    if (!sendBattleNetV2Packet(AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_AUTH_RESPONSE), generatedPayload.contents(), static_cast<uint32_t>(generatedPayload.size())))
    {
        return false;
    }

    return true;
}

bool WorldSocket::finalizeBattleNetV2WorldSession()
{
    if (m_battleNetV2State != BattleNetV2State::Encrypted || m_battleNetGameAccountId == 0 || m_battleNetGameAccountName.empty())
    {
        sLogger.failure("WorldSocket::BattleNetV2: cannot finalize session: state={}, battlenet_account={}, game_account={}, game_account_name='{}'.", static_cast<uint32_t>(m_battleNetV2State), m_battleNetAccountId, m_battleNetGameAccountId, m_battleNetGameAccountName);
        return false;
    }

    if (m_session != nullptr)
    {
        sLogger.failure("WorldSocket::BattleNetV2: socket already owns a WorldSession while finalizing WoW game account {}.", m_battleNetGameAccountId);
        return false;
    }

    if (WorldSession* existing = sWorld.getSessionByAccountId(m_battleNetGameAccountId))
    {
        sLogger.info("WorldSocket::BattleNetV2: replacing existing WorldSession for WoW game account {} before modern login.", m_battleNetGameAccountId);
        existing->Disconnect();
    }

    // WorldSession account ids are AscEmu WoW game-account ids (accounts.id),
    // not Battle.net account ids. Keeping the two identities separate is
    // essential once one Battle.net login owns multiple WoW accounts.
    auto sessionHolder = std::make_unique<WorldSession>(m_battleNetGameAccountId, m_battleNetGameAccountName, this);
    m_session = sessionHolder.get();
    m_session->SetClientBuild(m_battleNetClientBuild);
    m_session->LoadSecurity("");
    // Midnight entitlement/race availability is negotiated by the modern
    // protocol. The legacy Player::create() helper still checks AscEmu's old
    // expansion flags internally, so expose the full legacy baseline here
    // instead of accidentally rejecting valid modern characters.
    m_session->SetAccountFlags(AF_FULL_MOP);
    m_session->m_lastPing = static_cast<uint32_t>(UNIXTIME);
    m_session->_latency = m_latency;
    m_session->SetGlobalUpdateOwner();

    sLogger.info("WorldSocket::BattleNetV2: WorldSession created battlenet_account={} game_account={} name='{}' build={} region={} realm={}.", m_battleNetAccountId, m_battleNetGameAccountId, m_battleNetGameAccountName, m_battleNetClientBuild, m_battleNetRegionId, m_battleNetRealmId);

    if (!sendBattleNetV2AuthResponse())
    {
        sLogger.failure("WorldSocket::BattleNetV2: failed to send encrypted SMSG_AUTH_RESPONSE for WoW game account {}.", m_battleNetGameAccountId);
        m_session = nullptr;
        return false;
    }

    // Midnight protocol reference initializes the glue screen immediately after
    // SMSG_AUTH_RESPONSE. AscEmu's legacy WorldSession bootstrap cannot be used
    // for a Battle.net World V2 socket because those packets would go through
    // the old opcode/encryption path, so serialize the small modern bootstrap
    // packets here and send them through the AES-GCM bridge.
    //
    // Opcodes verified against Midnight protocol reference 12.1.0.69814:
    //   SMSG_SET_TIME_ZONE_INFORMATION         0x00450123
    //   SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN 0x00450064
    //   SMSG_CACHE_VERSION                     0x0049000E
    //   SMSG_AVAILABLE_HOTFIXES                0x00490001
    //   SMSG_ACCOUNT_DATA_TIMES                0x004501B6
    //   SMSG_TUTORIAL_FLAGS                    0x00450268
    const uint32_t virtualRealmAddress =
        ((m_battleNetRegionId & 0xFFU) << 24U) |
        (1U << 16U) |
        (m_battleNetRealmId & 0xFFFFU);

    auto sendGluePacket = [this](uint32_t opcode, ByteBuffer& payload, const char* name) -> bool
    {
        const uint32_t payloadSize = static_cast<uint32_t>(payload.size());
        if (!sendBattleNetV2Packet(opcode, payloadSize != 0U ? payload.contents() : nullptr, payloadSize))
        {
            sLogger.failure("WorldSocket::BattleNetV2: failed to send {} opcode=0x{:08X}.", name, opcode);
            return false;
        }
        return true;
    };

    // SetTimeZoneInformation::Write(): three 7-bit string lengths followed by
    // the raw strings. "Etc/UTC" is a client-supported IANA timezone and keeps
    // the bootstrap independent of the host OS timezone database.
    {
        const uint32_t SMSG_SET_TIME_ZONE_INFORMATION_RAW = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_SET_TIME_ZONE_INFORMATION);
        constexpr std::string_view TIMEZONE = "Etc/UTC";
        ByteBuffer packet;
        packet.writeBits(static_cast<uint32_t>(TIMEZONE.size()), 7);
        packet.writeBits(static_cast<uint32_t>(TIMEZONE.size()), 7);
        packet.writeBits(static_cast<uint32_t>(TIMEZONE.size()), 7);
        packet.flushBits();
        packet.append(reinterpret_cast<const uint8_t*>(TIMEZONE.data()), TIMEZONE.size());
        packet.append(reinterpret_cast<const uint8_t*>(TIMEZONE.data()), TIMEZONE.size());
        packet.append(reinterpret_cast<const uint8_t*>(TIMEZONE.data()), TIMEZONE.size());
        if (!sendGluePacket(SMSG_SET_TIME_ZONE_INFORMATION_RAW, packet, "SMSG_SET_TIME_ZONE_INFORMATION"))
            return false;
    }

    // FeatureSystemStatusGlueScreen::Write(). Keep optional services disabled,
    // expose the normal game mode (GameMode.db2 id 8), and advertise the same
    // expansion level used by SMSG_AUTH_RESPONSE.
    {
        const uint32_t SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN_RAW = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN);
        constexpr int32_t MAX_CHARACTERS_ON_REALM = 60;
        constexpr int32_t MINIMUM_EXPANSION_LEVEL = 0;
        constexpr int32_t MAXIMUM_EXPANSION_LEVEL = 11;
        constexpr int32_t STANDARD_GAME_MODE_ID = 8;

        ByteBuffer packet;

        // 43 bits before FlushBits(), exactly matching Midnight protocol reference's writer.
        packet.writeBit(0); // BpayStoreAvailable
        packet.writeBit(0); // CharUndeleteEnabled
        packet.writeBit(0); // CommerceServerEnabled
        packet.writeBit(0); // PaidCharacterTransfersBetweenBnetAccountsEnabled
        packet.writeBit(0); // VeteranTokenRedeemWillKick
        packet.writeBit(0); // WorldTokenRedeemWillKick
        packet.writeBit(0); // ExpansionPreorderInStore
        packet.writeBit(0); // KioskModeEnabled

        packet.writeBit(0); // CompetitiveModeEnabled
        packet.writeBit(0); // BoostEnabled
        packet.writeBit(0); // TrialBoostEnabled
        packet.writeBit(0); // RedeemForBalanceAvailable
        packet.writeBit(0); // LiveRegionCharacterListEnabled
        packet.writeBit(0); // LiveRegionCharacterCopyEnabled
        packet.writeBit(0); // LiveRegionAccountCopyEnabled
        packet.writeBit(0); // LiveRegionKeyBindingsCopyEnabled

        packet.writeBit(0); // BrowserCrashReporterEnabled
        packet.writeBit(0); // IsEmployeeAccount
        packet.writeBit(0); // UseBleep
        packet.writeBit(0); // EuropaTicketSystemStatus optional
        packet.writeBit(0); // NameReservationOnly
        packet.writeBit(0); // LaunchDurationETA optional
        packet.writeBit(0); // TimerunningEnabled
        packet.writeBit(0); // ScriptsDisallowedForBeta

        packet.writeBit(0); // PlayerIdentityOptionsEnabled
        packet.writeBit(0); // AccountExportEnabled
        packet.writeBit(0); // AccountLockedPostExport
        packet.writeBits(0U, 11); // RealmHiddenAlert length
        packet.writeBit(0); // CharacterSelectListModeRealmless
        packet.writeBit(0); // WowTokenLimitedMode
        packet.writeBit(0); // NavBarEnabled
        packet.writeBit(0); // GlobalUserGeneratedContentMuteEnabled
        packet.writeBit(0); // AccountUserGeneratedContentIsRisky
        packet.flushBits();

        packet << uint32_t(0); // CommercePricePollTimeSeconds
        packet << uint32_t(0); // KioskSessionDurationMinutes
        packet << int64_t(0);  // RedeemForBalanceAmount
        packet << int32_t(MAX_CHARACTERS_ON_REALM);
        packet << uint32_t(0); // LiveRegionCharacterCopySourceRegions count
        packet << int32_t(0);  // ActiveBoostType
        packet << int32_t(0);  // TrialBoostType
        packet << int32_t(MINIMUM_EXPANSION_LEVEL);
        packet << int32_t(MAXIMUM_EXPANSION_LEVEL);
        packet << int32_t(0);  // ContentSetID
        packet << uint32_t(0); // DisabledGameModes count
        packet << uint32_t(0); // GameRules count
        packet << uint32_t(1); // AvailableGameModeIDs count
        packet << int32_t(0);  // ActiveTimerunningSeasonID
        packet << int32_t(0);  // RemainingTimerunningSeasonSeconds
        packet << int32_t(86400); // TimerunningConversionMinCharacterAge = 1 day
        packet << int32_t(-1); // TimerunningConversionMaxSeasonID
        packet << int16_t(50); // MaxPlayerGuidLookupsPerRequest
        packet << int16_t(600); // NameLookupTelemetryInterval
        packet << uint32_t(10); // NotFoundCacheTimeSeconds
        packet << uint32_t(0);  // DebugTimeEvents count
        packet << int32_t(0);   // MostRecentTimeEventID
        packet << uint32_t(0);  // EventRealmQueues
        packet << int32_t(STANDARD_GAME_MODE_ID);

        if (!sendGluePacket(SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN_RAW, packet, "SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN"))
            return false;
    }

    // ClientCacheVersion::Write(): uint32 cache version. reference implementation defaults this
    // config to zero until DB loading supplies a value, so zero is a valid
    // bootstrap value for AscEmu while no Midnight hotfix DB exists.
    {
        const uint32_t SMSG_CACHE_VERSION_RAW = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_CACHE_VERSION);
        ByteBuffer packet;
        packet << uint32_t(0);
        if (!sendGluePacket(SMSG_CACHE_VERSION_RAW, packet, "SMSG_CACHE_VERSION"))
            return false;
    }

    // AvailableHotfixes::Write(): VirtualRealmAddress + uint32 count + records.
    // AscEmu currently has no hotfix store, therefore the list is empty.
    {
        const uint32_t SMSG_AVAILABLE_HOTFIXES_RAW = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_AVAILABLE_HOTFIXES);
        ByteBuffer packet;
        packet << int32_t(virtualRealmAddress);
        packet << uint32_t(0);
        if (!sendGluePacket(SMSG_AVAILABLE_HOTFIXES_RAW, packet, "SMSG_AVAILABLE_HOTFIXES"))
            return false;
    }

    // AccountDataTimes::Write(): packed ObjectGuid, int64 ServerTime and twenty
    // int64 account-data timestamps. The global account-data cache is not yet
    // backed by Midnight data in AscEmu, so every timestamp starts at zero.
    {
        const uint32_t SMSG_ACCOUNT_DATA_TIMES_RAW = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_ACCOUNT_DATA_TIMES);
        ByteBuffer packet;
        appendMidnightObjectGuid(packet, WoWGuid::createModernEmpty());
        packet << int64_t(UNIXTIME);
        for (uint32_t i = 0; i < 20U; ++i)
            packet << int64_t(0);
        if (!sendGluePacket(SMSG_ACCOUNT_DATA_TIMES_RAW, packet, "SMSG_ACCOUNT_DATA_TIMES"))
            return false;
    }

    // TutorialFlags::Write(): eight uint32 values.
    {
        const uint32_t SMSG_TUTORIAL_FLAGS_RAW = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_TUTORIAL_FLAGS);
        ByteBuffer packet;
        for (uint32_t i = 0; i < 8U; ++i)
            packet << uint32_t(0);
        if (!sendGluePacket(SMSG_TUTORIAL_FLAGS_RAW, packet, "SMSG_TUTORIAL_FLAGS"))
            return false;
    }

    // Battlenet::ConnectionStatus::Write(): reference implementation sends this immediately
    // after the glue/bootstrap packets once the realm-side Battle.net state is
    // available. State uses 2 bits and SuppressNotification uses 1 bit. The
    // reference implementation packet defaults SuppressNotification to true and only sets
    // State=1 here.
    {
        const uint32_t SMSG_BATTLE_NET_CONNECTION_STATUS_RAW = AscEmu::Version::Midnight::sOpcodeTable.getHexValueForInternalId(AscEmu::Version::Midnight::Opcode::SMSG_BATTLE_NET_CONNECTION_STATUS);
        ByteBuffer packet;
        packet.writeBits(1U, 2); // State = connected
        packet.writeBit(1);      // SuppressNotification = true (reference implementation default)
        packet.flushBits();
        if (!sendGluePacket(SMSG_BATTLE_NET_CONNECTION_STATUS_RAW, packet, "SMSG_BATTLE_NET_CONNECTION_STATUS"))
            return false;
    }

    // addSession(..., false) deliberately suppresses AscEmu's legacy
    // SMSG_ACCOUNT_DATA_TIMES packet. Modern packets must go through the V2
    // AES-GCM bridge instead of WowCrypt / the old opcode table.
    sWorld.addGlobalSession(m_session);
    sWorld.addSession(std::move(sessionHolder), false);
    isAuthenticated = true;

    sLogger.info("WorldSocket::BattleNetV2: WorldSession handoff complete for battlenet_account={} game_account={}; modern encrypted session is now active.", m_battleNetAccountId, m_battleNetGameAccountId);
    return true;
}

bool WorldSocket::encryptBattleNetV2Payload(std::vector<uint8_t>& data, std::array<uint8_t, 12>& tag)
{
    if (data.empty())
        return false;

    std::array<uint8_t, 12> iv{};
    memcpy(iv.data(), &m_battleNetV2CryptoSendCounter, sizeof(m_battleNetV2CryptoSendCounter));
    memcpy(iv.data() + sizeof(m_battleNetV2CryptoSendCounter), &AscEmu::Version::Midnight::BattleNet::ServerIvMagic, sizeof(AscEmu::Version::Midnight::BattleNet::ServerIvMagic));

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr)
        return false;

    bool success = EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    if (success)
        success = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) == 1;
    if (success)
        success = EVP_EncryptInit_ex(context, nullptr, nullptr, m_battleNetEncryptionKey.data(), iv.data()) == 1;

    int outputLength = 0;
    if (success)
        success = EVP_EncryptUpdate(context, data.data(), &outputLength, data.data(), static_cast<int>(data.size())) == 1 &&
            outputLength == static_cast<int>(data.size());

    int finalLength = 0;
    if (success)
        success = EVP_EncryptFinal_ex(context, data.data() + outputLength, &finalLength) == 1 && finalLength == 0;
    if (success)
        success = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) == 1;

    EVP_CIPHER_CTX_free(context);

    if (success)
        ++m_battleNetV2CryptoSendCounter;

    return success;
}

bool WorldSocket::decryptBattleNetV2Payload(std::vector<uint8_t>& data, const std::array<uint8_t, 12>& tag)
{
    if (data.empty())
        return false;

    std::array<uint8_t, 12> iv{};
    memcpy(iv.data(), &m_battleNetV2CryptoRecvCounter, sizeof(m_battleNetV2CryptoRecvCounter));
    memcpy(iv.data() + sizeof(m_battleNetV2CryptoRecvCounter), &AscEmu::Version::Midnight::BattleNet::ClientIvMagic, sizeof(AscEmu::Version::Midnight::BattleNet::ClientIvMagic));

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr)
        return false;

    bool success = EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    if (success)
        success = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) == 1;
    if (success)
        success = EVP_DecryptInit_ex(context, nullptr, nullptr, m_battleNetEncryptionKey.data(), iv.data()) == 1;

    int outputLength = 0;
    if (success)
        success = EVP_DecryptUpdate(context, data.data(), &outputLength, data.data(), static_cast<int>(data.size())) == 1 &&
            outputLength == static_cast<int>(data.size());

    std::array<uint8_t, AscEmu::Version::Midnight::BattleNet::AuthTagSize> mutableTag = tag;
    if (success)
        success = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, static_cast<int>(mutableTag.size()), mutableTag.data()) == 1;

    int finalLength = 0;
    if (success)
        success = EVP_DecryptFinal_ex(context, data.data() + outputLength, &finalLength) == 1 && finalLength == 0;

    EVP_CIPHER_CTX_free(context);

    if (success)
        ++m_battleNetV2CryptoRecvCounter;

    return success;
}



