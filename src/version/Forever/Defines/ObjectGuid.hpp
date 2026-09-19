/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AscEmu::Version::Forever
{
    // Forever 128-bit ObjectGuid wire representation, matching modern WoW protocol's
    // current ObjectGuid layout/packing for the types used by the bridge.
    enum class HighGuid : uint8_t
    {
        Null   = 0,
        Player = 2,
        Guild  = 28
    };

    class ObjectGuid
    {
    public:
        static constexpr std::size_t BytesSize = 16;

        constexpr ObjectGuid() = default;
        constexpr ObjectGuid(uint64_t high, uint64_t low) : m_data{ low, high } { }

        static constexpr ObjectGuid createPlayer(uint32_t realmId, uint64_t dbId, uint8_t subType = 0, uint32_t arg1 = 0)
        {
            return ObjectGuid((uint64_t(HighGuid::Player) << 58U) | (uint64_t(realmId) << 42U) | (uint64_t(subType & 0x3U) << 40U) | (uint64_t(arg1 & 0xFFFFFFU) << 16U), dbId);
        }

        static constexpr ObjectGuid createGuild(uint32_t realmId, uint64_t dbId)
        {
            return ObjectGuid((uint64_t(HighGuid::Guild) << 58U) | (uint64_t(realmId) << 42U), dbId);
        }

        static constexpr ObjectGuid empty() { return ObjectGuid(); }

        constexpr uint64_t getRawValue(std::size_t index) const { return m_data[index]; }
        constexpr uint64_t getLow() const { return m_data[0]; }
        constexpr HighGuid getHighType() const { return static_cast<HighGuid>((m_data[1] >> 58U) & 0x3FU); }
        constexpr uint32_t getRealmId() const { return static_cast<uint32_t>((m_data[1] >> 42U) & 0xFFFFU); }
        constexpr bool isEmpty() const { return m_data[0] == 0 && m_data[1] == 0; }

        static bool unpack(const uint8_t* data, std::size_t size, ObjectGuid& guid)
        {
            if (data == nullptr || size < sizeof(uint16_t))
                return false;

            const uint16_t mask = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
            std::array<uint8_t, BytesSize> raw{};
            std::size_t offset = sizeof(uint16_t);

            for (std::size_t i = 0; i < raw.size(); ++i)
            {
                if ((mask & (uint16_t(1U) << i)) == 0)
                    continue;

                if (offset >= size)
                    return false;

                raw[i] = data[offset++];
            }

            if (offset != size)
                return false;

            uint64_t low = 0;
            uint64_t high = 0;
            for (std::size_t i = 0; i < sizeof(uint64_t); ++i)
            {
                low |= static_cast<uint64_t>(raw[i]) << (i * 8U);
                high |= static_cast<uint64_t>(raw[i + sizeof(uint64_t)]) << (i * 8U);
            }

            guid = ObjectGuid(high, low);
            return true;
        }

        // Equivalent to modern WoW protocol's ByteBuffer << ObjectGuid operator:
        // 16 raw bytes are inspected in native little-endian order, a 16-bit
        // presence mask is written first, followed by every non-zero byte.
        std::vector<uint8_t> pack() const
        {
            std::array<uint8_t, BytesSize> raw{};
            for (std::size_t word = 0; word < m_data.size(); ++word)
            {
                for (std::size_t byte = 0; byte < sizeof(uint64_t); ++byte)
                    raw[word * sizeof(uint64_t) + byte] =
                        static_cast<uint8_t>((m_data[word] >> (byte * 8U)) & 0xFFU);
            }

            uint16_t mask = 0;
            std::vector<uint8_t> packed;
            packed.reserve(2 + BytesSize);
            packed.push_back(0);
            packed.push_back(0);

            for (std::size_t i = 0; i < raw.size(); ++i)
            {
                if (raw[i] == 0)
                    continue;

                mask |= static_cast<uint16_t>(uint16_t(1U) << i);
                packed.push_back(raw[i]);
            }

            packed[0] = static_cast<uint8_t>(mask & 0xFFU);
            packed[1] = static_cast<uint8_t>((mask >> 8U) & 0xFFU);
            return packed;
        }

    private:
        // modern WoW protocol stores low first, high second. This order is significant
        // because packed GUID serialization walks the raw 16 bytes directly.
        std::array<uint64_t, 2> m_data{};
    };
}
