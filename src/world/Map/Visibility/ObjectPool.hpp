/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <tuple>
#include <vector>

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

        // explicit move
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

    //////////////////////////////////////////////////////////////////////////////////////////
    /// ObjectPool
    ///
    /// Dynamically growing object slot pool used by the visibility system.
    ///
    /// Slot index 0 is reserved as invalid handle value. Valid handles always use slot ids
    /// greater than 0.
    ///
    /// The pool grows on demand when no free slot is available. Shrinking is conservative and
    /// only removes unused slots from the end of the slot storage, so currently valid handles
    /// are never invalidated by compaction.
    ///
    /// Handles are generation checked to prevent stale access after a slot was released and
    /// later reused.
    ///
    /// Threading:
    /// - allocate(), release() and shrinkToFit() are protected by the pool mutex.
    /// - Direct ObjectSlot access returned by tryGet() still requires the caller to respect
    ///   the visibility system ownership rules.
    //////////////////////////////////////////////////////////////////////////////////////////
    class ObjectPool
    {
    public:
        /// Create an empty object pool.
        ///
        /// Slot 0 is created immediately and kept reserved as invalid handle value.
        explicit ObjectPool(std::size_t initialCapacity = 0)
        {
            // Slot 0 is never allocated. It represents an invalid ObjectHandle.
            slots_.resize(1);
            next_.resize(1, 0);

            if (initialCapacity > 0)
                grow(initialCapacity);
        }

        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;
        ObjectPool(ObjectPool&&) = delete;
        ObjectPool& operator=(ObjectPool&&) = delete;

        //----------------------------------------------------------------------------------
        // Allocate / Release
        //----------------------------------------------------------------------------------

        /// Allocate a slot and return its generation checked handle.
        ///
        /// The pool grows automatically if no free slot is available.
        ObjectHandle allocate()
        {
            std::lock_guard<std::mutex> guard(mutex_);

            if (freeHead_ == 0)
            {
                const std::size_t currentCapacity = capacityUnlocked();

                // Grow gradually for small pools and exponentially for larger pools.
                const std::size_t growBy = currentCapacity < 64 ? 16 : currentCapacity / 2;
                grow(growBy);
            }

            const std::uint32_t id = freeHead_;
            freeHead_ = next_[id];
            next_[id] = 0;

            auto& slot = slots_[id];
            slot.inUse.store(true, std::memory_order_release);

            ++liveCount_;

            return { id, slot.gen.load(std::memory_order_acquire) };
        }

        /// Release a previously allocated handle back to the pool.
        ///
        /// Invalid, stale or already released handles are ignored.
        bool release(ObjectHandle h)
        {
            std::lock_guard<std::mutex> guard(mutex_);

            if (h.id == 0 || h.id >= slots_.size())
                return false;

            auto& slot = slots_[h.id];

            if (slot.gen.load(std::memory_order_acquire) != h.gen)
                return false;

            bool expected = true;
            if (!slot.inUse.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
                return false;

            resetSlot(slot);

            // Bump generation after the slot was released so old handles become stale.
            slot.gen.fetch_add(1, std::memory_order_acq_rel);

            next_[h.id] = freeHead_;
            freeHead_ = h.id;

            --liveCount_;

            return true;
        }

        //----------------------------------------------------------------------------------
        // Access
        //----------------------------------------------------------------------------------

        /// Try to obtain a mutable pointer to the slot for a valid handle.
        bool tryGet(ObjectHandle h, ObjectSlot*& out)
        {
            if (h.id == 0 || h.id >= slots_.size())
                return false;

            auto& slot = slots_[h.id];

            if (!slot.inUse.load(std::memory_order_acquire))
                return false;

            if (slot.gen.load(std::memory_order_acquire) != h.gen)
                return false;

            out = &slot;
            return true;
        }

        /// Try to obtain a const pointer to the slot for a valid handle.
        bool tryGet(ObjectHandle h, const ObjectSlot*& out) const
        {
            if (h.id == 0 || h.id >= slots_.size())
                return false;

            const auto& slot = slots_[h.id];

            if (!slot.inUse.load(std::memory_order_acquire))
                return false;

            if (slot.gen.load(std::memory_order_acquire) != h.gen)
                return false;

            out = &slot;
            return true;
        }

        //----------------------------------------------------------------------------------
        // Init / Meta / Pos
        //----------------------------------------------------------------------------------

        /// Initialize an allocated slot.
        ///
        /// Returns false if the handle is invalid or stale.
        bool init(ObjectHandle h, const ObjectMeta& meta, const LocationVector& pos, void* native = nullptr)
        {
            ObjectSlot* slot = nullptr;
            if (!tryGet(h, slot))
                return false;

            slot->meta = meta;
            slot->pos = pos;
            slot->native = native;
            slot->sub = {};
            slot->pub = {};
            slot->interest = {};
            slot->lastObjectInterestRefreshValid = false;
            slot->lastObjectInterestRefreshPos = {};
            slot->visibilityQueryStamp = 0;
            slot->nearCache = {};
            slot->nearCache.forceInvalidate = true;

            return true;
        }

        /// Returns mutable metadata without validating the handle.
        ///
        /// Caller must guarantee that the handle is valid.
        ObjectMeta& meta(ObjectHandle h)
        {
            return slots_[h.id].meta;
        }

        /// Returns immutable metadata without validating the handle.
        ///
        /// Caller must guarantee that the handle is valid.
        const ObjectMeta& meta(ObjectHandle h) const
        {
            return slots_[h.id].meta;
        }

        /// Returns mutable position without validating the handle.
        ///
        /// Caller must guarantee that the handle is valid.
        LocationVector& pos(ObjectHandle h)
        {
            return slots_[h.id].pos;
        }

        /// Returns immutable position without validating the handle.
        ///
        /// Caller must guarantee that the handle is valid.
        const LocationVector& pos(ObjectHandle h) const
        {
            return slots_[h.id].pos;
        }

        //----------------------------------------------------------------------------------
        // Pool Stats / Maintenance
        //----------------------------------------------------------------------------------

        /// Number of currently allocated slots.
        std::uint32_t liveCount() const
        {
            std::lock_guard<std::mutex> guard(mutex_);
            return liveCount_;
        }

        /// Number of usable slots, excluding reserved slot 0.
        std::uint32_t capacity() const
        {
            std::lock_guard<std::mutex> guard(mutex_);
            return static_cast<std::uint32_t>(capacityUnlocked());
        }

        /// Try to release unused memory from the end of the pool.
        ///
        /// Only free slots at the end can be removed. Live slots are never moved.
        ///
        /// \param minCapacity Minimum number of usable slots to keep.
        /// \param maxFreeRatio Shrink only if free slots exceed live slots by this ratio.
        void shrinkToFit(std::size_t minCapacity = 64, std::size_t maxFreeRatio = 4)
        {
            std::lock_guard<std::mutex> guard(mutex_);

            const std::size_t usableCapacity = capacityUnlocked();
            if (usableCapacity <= minCapacity)
                return;

            const std::size_t freeCount = usableCapacity - liveCount_;
            if (liveCount_ > 0 && freeCount < liveCount_ * maxFreeRatio)
                return;

            rebuildFreeListWithoutTrailingFreeSlots(minCapacity);
        }

    private:
        /// Number of usable slots, excluding reserved slot 0.
        std::size_t capacityUnlocked() const noexcept
        {
            return slots_.empty() ? 0 : slots_.size() - 1;
        }

        /// Add new slots and link them into the free-list.
        void grow(std::size_t count)
        {
            if (count == 0)
                return;

            const std::size_t oldSize = slots_.size();
            const std::size_t newSize = oldSize + count;

            slots_.resize(newSize);
            next_.resize(newSize, 0);

            for (std::size_t i = oldSize; i < newSize; ++i)
            {
                const auto id = static_cast<std::uint32_t>(i);

                slots_[i].gen.store(1, std::memory_order_relaxed);
                slots_[i].inUse.store(false, std::memory_order_relaxed);

                next_[i] = freeHead_;
                freeHead_ = id;
            }
        }

        /// Reset all non-handle state of a released slot.
        void resetSlot(ObjectSlot& slot)
        {
            slot.native = nullptr;
            slot.sub = {};
            slot.pub = {};
            slot.interest = {};
            slot.lastObjectInterestRefreshValid = false;
            slot.lastObjectInterestRefreshPos = {};
            slot.visibilityQueryStamp = 0;
            slot.nearCache.clearAndRelease();
        }

        /// Rebuild the free-list and remove unused trailing slots.
        ///
        /// This does not move live slots. It only removes free slots from the end of the
        /// storage and then rebuilds the free-list for all remaining free slots.
        void rebuildFreeListWithoutTrailingFreeSlots(std::size_t minCapacity)
        {
            std::size_t newSize = slots_.size();

            while (newSize > minCapacity + 1)
            {
                const ObjectSlot& slot = slots_[newSize - 1];

                if (slot.inUse.load(std::memory_order_acquire))
                    break;

                --newSize;
            }

            if (newSize == slots_.size())
                return;

            while (slots_.size() > newSize)
                slots_.pop_back();

            next_.resize(newSize, 0);

            freeHead_ = 0;

            for (std::size_t i = 1; i < slots_.size(); ++i)
            {
                if (slots_[i].inUse.load(std::memory_order_acquire))
                    continue;

                const auto id = static_cast<std::uint32_t>(i);
                next_[i] = freeHead_;
                freeHead_ = id;
            }

        }

    private:
        std::deque<ObjectSlot>      slots_{};
        std::vector<std::uint32_t>  next_{};
        std::uint32_t               freeHead_{ 0 };
        std::uint32_t               liveCount_{ 0 };
        mutable std::mutex          mutex_;
    };


} // namespace visibility
