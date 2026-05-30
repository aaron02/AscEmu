/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <vector>
#include <tuple>
#include <atomic>
#include <cstdint>

#include "VisibilityTypes.hpp"

namespace visibility 
{
    ////////////////////////////////////////////////////////////////////////////////////////
    /// NearCache
    ///
    /// Small helper cache kept per object to speed up "nearby set" computations.
    /// Force invalidation whenever an object (or its grid) moves.
    ////////////////////////////////////////////////////////////////////////////////////////
    struct NearCache
    {
        int centerGid = -1;
        int lcx = -1;
        int lcy = -1;
        int radius = 2;

        std::vector<std::tuple<int, int, uint32_t>> stamps;
        std::vector<WoWGuid> guids;
        bool forceInvalidate = true;

        /// Mark the cache as invalid so it will be rebuilt on next use.
        void invalidate() { forceInvalidate = true; }

        /// Clear and release cached allocations. Use only on object release/unload,
        /// not on normal movement invalidation.
        void clearAndRelease()
        {
            centerGid = -1;
            lcx = -1;
            lcy = -1;
            radius = 2;

            stamps.clear();
            guids.clear();
            stamps.shrink_to_fit();
            guids.shrink_to_fit();

            forceInvalidate = true;
        }

        /// Trim excessive retained vector capacity after a cache rebuild.
        /// This is intentionally conservative and is never called on movement
        /// invalidation or cache hits.
        void trimExcessCapacity()
        {
            constexpr std::size_t GuidCapacityFloor = 4096;
            constexpr std::size_t StampCapacityFloor = 512;

            if (guids.capacity() > GuidCapacityFloor && guids.capacity() > guids.size() * 4)
                std::vector<WoWGuid>(guids).swap(guids);

            if (stamps.capacity() > StampCapacityFloor && stamps.capacity() > stamps.size() * 4)
                std::vector<std::tuple<int, int, uint32_t>>(stamps).swap(stamps);
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////
    /// ObjectSlot
    ///
    /// A single slot inside the ObjectPool storing metadata, position and optional
    /// back-reference to a native engine object.
    ////////////////////////////////////////////////////////////////////////////////////////
    struct ObjectSlot 
    {
        // Handle Generation & Use-Flag
        std::atomic<std::uint32_t>  gen{ 1 };
        std::atomic<bool>           inUse{ false };

        // Meta / Position / Backref
        ObjectMeta                  meta{};
        LocationVector              pos{};
        void*                       native{ nullptr };  // optional Back-Ref (no Ownership)
        SubState                    sub{};              // subscription state
        PubState                    pub{};              // broadcast state
        InterestProfile             interest{};         // interest / publish profile

        // Last position that triggered an expensive moved-object interest refresh.
        // SpatialIndex::moveObject() still updates the position and invalidates the
        // object's own near cache on every movement; this only throttles visibility
        // pair checks for same-cell micro movement.
        bool                        lastObjectInterestRefreshValid{ false };
        LocationVector              lastObjectInterestRefreshPos{};

        // Scratch stamp used by VisibilitySystem candidate collectors to dedupe
        // without per-query unordered_set allocations. Map-thread only.
        mutable std::uint64_t       visibilityQueryStamp{ 0 };

        mutable NearCache           nearCache;

        ObjectSlot() = default;

        // no copy
        ObjectSlot(const ObjectSlot&) = delete;
        ObjectSlot& operator=(const ObjectSlot&) = delete;

        // explicit move (to support std::vector reallocation)
        ObjectSlot(ObjectSlot&& other) noexcept 
        {
            gen.store(other.gen.load(std::memory_order_relaxed), std::memory_order_relaxed);
            inUse.store(other.inUse.load(std::memory_order_relaxed), std::memory_order_relaxed);
            meta = other.meta;
            pos = other.pos;
            native = other.native;
            sub = other.sub;
            pub = other.pub;
            interest = other.interest;
            lastObjectInterestRefreshValid = other.lastObjectInterestRefreshValid;
            lastObjectInterestRefreshPos = other.lastObjectInterestRefreshPos;
            visibilityQueryStamp = 0;
            // nearCache intentionally not moved to avoid stale references
            nearCache.invalidate();
        }

        ObjectSlot& operator=(ObjectSlot&& other) noexcept 
        {
            if (this != &other)
            {
                gen.store(other.gen.load(std::memory_order_relaxed), std::memory_order_relaxed);
                inUse.store(other.inUse.load(std::memory_order_relaxed), std::memory_order_relaxed);
                meta = other.meta;
                pos = other.pos;
                native = other.native;
                sub = other.sub;
                pub = other.pub;
                interest = other.interest;
                lastObjectInterestRefreshValid = other.lastObjectInterestRefreshValid;
                lastObjectInterestRefreshPos = other.lastObjectInterestRefreshPos;
                visibilityQueryStamp = 0;
                // nearCache intentionally not moved to avoid stale references
                nearCache.invalidate();
            }
            return *this;
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////
    /// ObjectPool
    ///
    /// Lock-free free-list based pool for fixed-capacity object slots.
    /// Provides generation-checked handles to avoid ABA/stale access.
    ////////////////////////////////////////////////////////////////////////////////////////
    class ObjectPool 
    {
    public:
        /// Create a pool with a fixed number of slots.
        explicit ObjectPool(std::size_t capacity)
        {
            slots_.resize(capacity);
            next_.resize(capacity);

            for (std::size_t i = 1; i < capacity - 1; ++i)
            {
                next_[i] = static_cast<std::uint32_t>(i + 1);
            }

            next_[capacity - 1] = 0; // end
            freeHead_.store(1);
            liveCount_.store(0, std::memory_order_relaxed);
        }

        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;
        ObjectPool(ObjectPool&&) = delete;
        ObjectPool& operator=(ObjectPool&&) = delete;

        //----------------------------------------------------------------------------------
        // Allocate / Release
        //----------------------------------------------------------------------------------

        /// Allocate a slot and return a handle. Returns {0,0} if pool is full.
        ObjectHandle allocate()
        {
            std::uint32_t head = freeHead_.load(std::memory_order_acquire);
            while (head != 0) 
            {
                const std::uint32_t nxt = next_[head];
                if (freeHead_.compare_exchange_weak(head, nxt, std::memory_order_acq_rel)) 
                {
                    auto& s = slots_[head];
                    s.inUse.store(true, std::memory_order_release);
                    liveCount_.fetch_add(1, std::memory_order_acq_rel);
                    return { head, s.gen.load(std::memory_order_acquire) };
                }
            }
            return {};
        }

        /// Release a previously allocated handle back to the pool.
        void release(ObjectHandle h)
        {
            if (h.id == 0 || h.id >= slots_.size())
                return;

            auto& s = slots_[h.id];
            s.inUse.store(false, std::memory_order_release);
            s.native = nullptr;
            s.sub = {};
            s.pub = {};
            s.interest = {};
            s.lastObjectInterestRefreshValid = false;
            s.lastObjectInterestRefreshPos = {};
            s.visibilityQueryStamp = 0;
            s.nearCache.clearAndRelease();
            s.gen.fetch_add(1, std::memory_order_acq_rel);
            liveCount_.fetch_sub(1, std::memory_order_acq_rel);
            std::uint32_t head = freeHead_.load(std::memory_order_acquire);
            do { next_[h.id] = head; } while (!freeHead_.compare_exchange_weak(head, h.id, std::memory_order_acq_rel));
        }

        //----------------------------------------------------------------------------------
        // Access
        //----------------------------------------------------------------------------------

        /// Try to obtain a mutable pointer to the slot for a valid handle.
        bool tryGet(ObjectHandle h, ObjectSlot*& out)
        {
            if (h.id >= slots_.size())
                return false;

            auto& s = slots_[h.id];
            if (!s.inUse.load(std::memory_order_acquire))
                return false;

            if (s.gen.load(std::memory_order_acquire) != h.gen)
                return false;

            out = &s;
            return true;
        }

        /// Try to obtain a const pointer to the slot for a valid handle.
        bool tryGet(ObjectHandle h, const ObjectSlot*& out) const
        {
            if (h.id >= slots_.size())
                return false;

            const auto& s = slots_[h.id];
            if (!s.inUse.load(std::memory_order_acquire))
                return false;

            if (s.gen.load(std::memory_order_acquire) != h.gen)
                return false;

            out = &s;
            return true;
        }

        //----------------------------------------------------------------------------------
        // Init / Meta / Pos
        //----------------------------------------------------------------------------------

        /// Initialize the slot's metadata and position, and set optional backref.
        void init(ObjectHandle h, const ObjectMeta& m, const LocationVector& p, void* native = nullptr)
        {
            if (h.id == 0 || h.id >= slots_.size())
                return;

            auto& s = slots_[h.id];
            s.meta = m;
            s.pos = p;
            s.native = native;
            s.sub = {};
            s.pub = {};
            s.interest = {};
            s.lastObjectInterestRefreshValid = false;
            s.lastObjectInterestRefreshPos = {};
            s.visibilityQueryStamp = 0;
            s.nearCache = {};
            s.nearCache.forceInvalidate = true;
        }

        ObjectMeta& meta(ObjectHandle h) { return slots_[h.id].meta; }
        const ObjectMeta& meta(ObjectHandle h) const { return slots_[h.id].meta; }

        LocationVector& pos(ObjectHandle h) { return slots_[h.id].pos; }
        const LocationVector& pos(ObjectHandle h) const { return slots_[h.id].pos; }

        // Number of currently live (allocated) slots.
        std::uint32_t liveCount() const noexcept { return liveCount_.load(std::memory_order_acquire); }

    private:
        std::vector<ObjectSlot>     slots_ {};
        std::vector<std::uint32_t>  next_ {};
        std::atomic<std::uint32_t>  freeHead_ { 0 };
        std::atomic<std::uint32_t>  liveCount_ { 0 };
    };

} // namespace visibility
