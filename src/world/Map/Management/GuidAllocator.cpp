/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "GuidAllocator.hpp"
#include <algorithm>

uint32_t GuidAllocator::allocateLow(uint32_t highTypeMasked, bool canReuse)
{
    const int idx = slotOf(highTypeMasked);
    if (idx < 0) throw std::logic_error("unsupported HighGuid for allocation");

    std::lock_guard<std::mutex> g(mtx_);

    if (canReuse && !reuse_[idx].empty()) {
        uint32_t id = reuse_[idx].front();
        reuse_[idx].pop_front();
        return id;
    }

    uint32_t& next = nextLow_[idx];

    if (next >= 0xFFFFFFu)                // 24-bit full
        throw std::overflow_error("24-bit id space exhausted");

    return ++next;
}

uint64_t GuidAllocator::packRaw(uint32_t highType, uint32_t entryId, uint32_t lowCounter)
{
    // HighGuid-Bits (12 Bits)
    const uint64_t highPart = (uint64_t(highType) & uint64_t(HIGHGUID_TYPE_MASK)) << 32;

    // entry and id 24 Bit
    if (entryId > 0x00FFFFFFu) throw std::out_of_range("entryId > 24-bit");
    if (lowCounter > 0x00FFFFFFu) throw std::out_of_range("lowCounter > 24-bit");

    const uint64_t entryPart = (uint64_t(entryId & 0x00FFFFFFu) << 24);
    const uint64_t lowPart = uint64_t(lowCounter & 0x00FFFFFFu);

    return highPart | entryPart | lowPart;
}

uint64_t GuidAllocator::allocCreature(uint32_t entry, bool canReuse, bool isVehicle)
{
    const uint32_t highType = isVehicle ? HIGHGUID_TYPE_VEHICLE : HIGHGUID_TYPE_UNIT;
    const uint32_t low = allocateLow(highType, canReuse);
    const uint64_t raw = packRaw(highType, entry, low);
    return raw;
}

uint64_t GuidAllocator::allocGameObject(uint32_t entry, bool canReuse)
{
    const uint32_t highType = HIGHGUID_TYPE_GAMEOBJECT;
    const uint32_t low = allocateLow(highType, canReuse);
    const uint64_t raw = packRaw(highType, entry, low);
    return raw;
}

uint64_t GuidAllocator::allocTransporter(uint32_t entry, bool canReuse)
{
    const uint32_t highType = HIGHGUID_TYPE_TRANSPORTER;
    const uint32_t low = allocateLow(highType, canReuse);
    const uint64_t raw = packRaw(highType, entry, low);
    return raw;
}

uint64_t GuidAllocator::allocDynamicObject(uint32_t entry, bool canReuse)
{
    const uint32_t highType = HIGHGUID_TYPE_DYNAMICOBJECT;
    const uint32_t low = allocateLow(highType, canReuse);
    const uint64_t raw = packRaw(highType, entry, low);
    return raw;
}

uint64_t GuidAllocator::allocCorpse(bool canReuse)
{
    const uint32_t highType = HIGHGUID_TYPE_CORPSE;
    const uint32_t low = allocateLow(highType, canReuse);
    const uint64_t raw = packRaw(highType, /*entry=*/0, low);
    return raw;
}

void GuidAllocator::release(const uint64_t& guid)
{
    const uint32_t typeMasked = extractHighType(guid);
    const int idx = slotOf(typeMasked);
    if (idx < 0) return;

    const uint32_t low = extractLow24(guid);
    std::lock_guard<std::mutex> lock(mtx_);
    if (reuse_[idx].size())
        reuse_[idx].push_back(low);
}
