/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <list>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "VisibilityTypes.hpp"
#include "ObjectPool.hpp"

#include "Objects/Object.hpp"
#include "Objects/Units/Unit.hpp"
#include "Objects/Units/Creatures/Creature.h"
#include "Objects/GameObject.h"

namespace visibility
{
    struct SpatialMoveResult
    {
        ObjectHandle handle{};
        bool valid{ false };
        bool moved{ false };
        bool cellChanged{ false };
        bool gridChanged{ false };

        int oldGrid{ 0 };
        int oldCell{ 0 };
        int newGrid{ 0 };
        int newCell{ 0 };

        LocationVector oldPos{};
        LocationVector newPos{};
    };

    class SpatialIndex
    {
    public:
        using GridMap = std::unordered_map<int, GridChunk>;

        explicit SpatialIndex(std::size_t poolCapacity)
            : pool_(poolCapacity)
        {
        }

        SpatialIndex(const SpatialIndex&) = delete;
        SpatialIndex& operator=(const SpatialIndex&) = delete;
        SpatialIndex(SpatialIndex&&) = delete;
        SpatialIndex& operator=(SpatialIndex&&) = delete;

        // Static spatial helpers.
        static int packGridFromPos(const LocationVector& p) noexcept { auto [gx, gy] = worldToGrid(p); return packGridId(gx, gy); }
        static int packCellFromPos(const LocationVector& p) noexcept { auto [cx, cy] = worldToLocal(p); return packCellId(cx, cy); }
        static int cellsForRadius(float radius) noexcept
        {
            if (radius <= 0.0f)
                return 0;

            return std::max(1, static_cast<int>(std::ceil(radius / Cell::Size)));
        }

        // Spatial lifecycle. SpatialIndex is the owner of handle/slot/cell membership.
        ObjectHandle addObject(const ObjectMeta& meta, const LocationVector& pos, void* native = nullptr)
        {
            ObjectHandle h = pool_.allocate();
            if (!h.id)
                return {};

            pool_.init(h, meta, pos, native);
            attachToCell(h, pos);

            {
                std::unique_lock<std::shared_mutex> lk(guidMtx_);
                guidToHandle_[meta.guid.getRawGuid()] = h;
            }

            return h;
        }

        bool removeObject(ObjectHandle h)
        {
            ObjectSlot* slot = nullptr;
            if (!pool_.tryGet(h, slot))
                return false;

            const auto guid = slot->meta.guid.getRawGuid();
            detachFromCell(h, slot->pos);

            {
                std::unique_lock<std::shared_mutex> lk(guidMtx_);
                guidToHandle_.erase(guid);
            }

            pool_.release(h);
            return true;
        }

        SpatialMoveResult moveObject(ObjectHandle h, const LocationVector& newPos)
        {
            SpatialMoveResult result;
            result.handle = h;

            ObjectSlot* slot = nullptr;
            if (!pool_.tryGet(h, slot))
                return result;

            result.valid = true;
            result.oldPos = slot->pos;
            result.newPos = newPos;
            result.oldGrid = packGridFromPos(result.oldPos);
            result.oldCell = packCellFromPos(result.oldPos);
            result.newGrid = packGridFromPos(newPos);
            result.newCell = packCellFromPos(newPos);
            result.gridChanged = result.oldGrid != result.newGrid;
            result.cellChanged = result.gridChanged || result.oldCell != result.newCell;
            result.moved = true;

            slot->pos = newPos;
            slot->nearCache.invalidate();

            if (result.gridChanged)
                transferCell(h, result.oldGrid, result.newGrid, result.oldCell, result.newCell);
            else if (result.oldCell != result.newCell)
                reindexWithinGrid(h, result.oldGrid, result.oldCell, result.newCell);

            return result;
        }

        ObjectHandle handleByGuid(const WoWGuid& guid) const noexcept
        {
            std::shared_lock<std::shared_mutex> lk(guidMtx_);
            auto it = guidToHandle_.find(guid.getRawGuid());
            return it == guidToHandle_.end() ? ObjectHandle{} : it->second;
        }

        // Object slot storage. SpatialIndex owns the handle -> slot mapping because
        // cells store ObjectHandle values and spatial queries resolve through slots.
        ObjectHandle allocate() { return pool_.allocate(); }
        void release(ObjectHandle h) { pool_.release(h); }
        void init(ObjectHandle h, const ObjectMeta& meta, const LocationVector& pos, void* native = nullptr)
        {
            pool_.init(h, meta, pos, native);
        }

        bool tryGet(ObjectHandle h, ObjectSlot*& out) { return pool_.tryGet(h, out); }
        bool tryGet(ObjectHandle h, const ObjectSlot*& out) const { return pool_.tryGet(h, out); }

        ObjectMeta& meta(ObjectHandle h) { return pool_.meta(h); }
        const ObjectMeta& meta(ObjectHandle h) const { return pool_.meta(h); }

        LocationVector& pos(ObjectHandle h) { return pool_.pos(h); }
        const LocationVector& pos(ObjectHandle h) const { return pool_.pos(h); }

        std::uint32_t liveCount() const noexcept { return pool_.liveCount(); }
        std::size_t guidCount() const noexcept
        {
            std::shared_lock<std::shared_mutex> lk(guidMtx_);
            return guidToHandle_.size();
        }

        GridChunk& getOrCreateGrid(int gid)
        {
            auto it = grids_.find(gid);
            if (it == grids_.end())
                it = grids_.try_emplace(gid).first;
            return it->second;
        }

        GridChunk* tryGetGrid(int gid)
        {
            auto it = grids_.find(gid);
            return it == grids_.end() ? nullptr : &it->second;
        }

        const GridChunk* tryGetGrid(int gid) const
        {
            auto it = grids_.find(gid);
            return it == grids_.end() ? nullptr : &it->second;
        }

        CellChunk& getOrCreateCell(GridChunk& grid, int lcid)
        {
            auto it = grid.cells.find(lcid);
            if (it == grid.cells.end())
                it = grid.cells.try_emplace(lcid).first;
            return it->second;
        }

        CellChunk* tryGetCell(GridChunk& grid, int lcid)
        {
            auto it = grid.cells.find(lcid);
            return it == grid.cells.end() ? nullptr : &it->second;
        }

        const CellChunk* tryGetCell(const GridChunk& grid, int lcid) const
        {
            auto it = grid.cells.find(lcid);
            return it == grid.cells.end() ? nullptr : &it->second;
        }

        std::vector<CellRef> buildRingCells(int baseGid, int lcx, int lcy, int radiusCells) const
        {
            std::vector<CellRef> out;
            out.reserve((2 * radiusCells + 1) * (2 * radiusCells + 1));

            auto [bgx, bgy] = unpackGridId(baseGid);

            for (int dy = -radiusCells; dy <= radiusCells; ++dy)
            {
                for (int dx = -radiusCells; dx <= radiusCells; ++dx)
                {
                    int cx = lcx + dx;
                    int cy = lcy + dy;
                    int gx = bgx;
                    int gy = bgy;

                    while (cx < 0) { cx += Cell::CellsPerTile; --gx; }
                    while (cy < 0) { cy += Cell::CellsPerTile; --gy; }
                    while (cx >= Cell::CellsPerTile) { cx -= Cell::CellsPerTile; ++gx; }
                    while (cy >= Cell::CellsPerTile) { cy -= Cell::CellsPerTile; ++gy; }

                    gx = std::clamp(gx, 0, Terrain::TilesCount - 1);
                    gy = std::clamp(gy, 0, Terrain::TilesCount - 1);

                    out.emplace_back(packGridId(gx, gy), packCellId(cx, cy));
                }
            }

            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        }


        ////////////////////////////////////////////////////////////////////////////////////
        /// Spatial queries
        ////////////////////////////////////////////////////////////////////////////////////
        void collectNearGuidsCached(const WoWGuid& centerGuid, int radiusCells, std::vector<WoWGuid>& out) const
        {
            auto h = handleByGuid(centerGuid);
            if (!h.id)
            {
                out.clear();
                return;
            }

            const ObjectSlot* centerSlot = nullptr;
            if (!tryGet(h, centerSlot))
            {
                out.clear();
                return;
            }

            const int gid = packGridFromPos(centerSlot->pos);
            auto [lcx, lcy] = worldToLocal(centerSlot->pos);

            bool valid = !centerSlot->nearCache.forceInvalidate
                && centerSlot->nearCache.centerGid == gid
                && centerSlot->nearCache.lcx == lcx
                && centerSlot->nearCache.lcy == lcy
                && centerSlot->nearCache.radius == radiusCells;

            if (valid)
            {
                std::size_t ok = 0;
                for (auto [cgid, clcid, cep] : centerSlot->nearCache.stamps)
                {
                    const GridChunk* grid = tryGetGrid(cgid);
                    if (!grid)
                    {
                        valid = false;
                        break;
                    }

                    const CellChunk* cell = tryGetCell(*grid, clcid);
                    if (!cell)
                    {
                        valid = false;
                        break;
                    }

                    const std::uint32_t now = cell->epoch.load(std::memory_order_relaxed);
                    if (now == cep)
                        ++ok;
                    else
                    {
                        valid = false;
                        break;
                    }
                }

                if (valid && ok == centerSlot->nearCache.stamps.size())
                {
                    out.clear();
                    out.reserve(centerSlot->nearCache.guids.size());
                    out.insert(out.end(), centerSlot->nearCache.guids.begin(), centerSlot->nearCache.guids.end());
                    return;
                }
            }

            const ObjectSlot* mutableSlot = nullptr;
            if (!tryGet(h, mutableSlot))
            {
                out.clear();
                return;
            }

            mutableSlot->nearCache.stamps.clear();
            mutableSlot->nearCache.guids.clear();

            std::unordered_set<std::uint64_t> dedupe;
            dedupe.reserve(256);
            dedupe.insert(centerGuid.getRawGuid());

            auto ring = buildRingCells(gid, lcx, lcy, radiusCells);
            for (auto [cellGid, cellLcid] : ring)
            {
                const GridChunk* grid = tryGetGrid(cellGid);
                if (!grid)
                    continue;

                const CellChunk* cell = tryGetCell(*grid, cellLcid);
                if (!cell)
                    continue;

                const std::uint32_t cellEpoch = cell->epoch.load(std::memory_order_relaxed);
                mutableSlot->nearCache.stamps.emplace_back(cellGid, cellLcid, cellEpoch);

                std::shared_lock<std::shared_mutex> lk(cell->mtx);
                for (std::size_t container = 0; container < static_cast<std::size_t>(Container::Count); ++container)
                {
                    for (auto objectHandle : cell->byContainer[container])
                    {
                        const ObjectSlot* objectSlot = nullptr;
                        if (!tryGet(objectHandle, objectSlot))
                            continue;

                        const std::uint64_t raw = objectSlot->meta.guid.getRawGuid();
                        if (dedupe.insert(raw).second)
                            mutableSlot->nearCache.guids.emplace_back(WoWGuid(raw));
                    }
                }
            }

            mutableSlot->nearCache.centerGid = gid;
            mutableSlot->nearCache.lcx = lcx;
            mutableSlot->nearCache.lcy = lcy;
            mutableSlot->nearCache.radius = radiusCells;
            mutableSlot->nearCache.forceInvalidate = false;
            mutableSlot->nearCache.trimExcessCapacity();

            out.clear();
            out.reserve(mutableSlot->nearCache.guids.size());
            out.insert(out.end(), mutableSlot->nearCache.guids.begin(), mutableSlot->nearCache.guids.end());
        }

        template<typename T>
        void collectGuidsInCell(int gid, int lcid, std::uint32_t entry, std::vector<WoWGuid>& out) const
        {
            const GridChunk* grid = tryGetGrid(gid);
            if (!grid)
                return;

            const CellChunk* cell = tryGetCell(*grid, lcid);
            if (!cell)
                return;

            constexpr std::size_t idx = static_cast<std::size_t>(TypeMap<T>::container);

            std::shared_lock<std::shared_mutex> lk(cell->mtx);
            const auto& list = cell->byContainer[idx];
            out.reserve(out.size() + list.size());

            for (auto h : list)
            {
                const ObjectSlot* slot = nullptr;
                if (!tryGet(h, slot) || !slot || !slot->native)
                    continue;

                T* obj = static_cast<T*>(slot->native);

                std::uint32_t objectEntry = 0;
                if constexpr (requires (T* t) { t->GetEntry(); })
                    objectEntry = obj->GetEntry();
                else if constexpr (requires (T* t) { t->getEntry(); })
                    objectEntry = obj->getEntry();

                if (entry == 0 || objectEntry == entry)
                    out.push_back(slot->meta.guid);
            }
        }

        template<typename T>
        void collectGuidsInCellsAroundPos(const LocationVector& center, int radiusCells, std::uint32_t entry, std::vector<WoWGuid>& out) const
        {
            const int baseGid = packGridFromPos(center);
            auto [lcx, lcy] = worldToLocal(center);

            for (auto [gid, lcid] : buildRingCells(baseGid, lcx, lcy, radiusCells))
                collectGuidsInCell<T>(gid, lcid, entry, out);
        }

        template<typename T>
        void collectByEntryInRange(const LocationVector& centerPos, std::list<T*>& out, std::uint32_t entry, float maxSearchRange) const
        {
            if (maxSearchRange < 0.0f)
                return;

            const float r2 = maxSearchRange * maxSearchRange;

            forEachContainerObjectInRange<T>(centerPos, maxSearchRange, [&](ObjectHandle h, const ObjectSlot& slot)
            {
                T* obj = static_cast<T*>(slot.native);
                if (!obj)
                    return;

                std::uint32_t objectEntry = 0;
                if constexpr (requires (T* t) { t->GetEntry(); })
                    objectEntry = obj->GetEntry();
                else if constexpr (requires (T* t) { t->getEntry(); })
                    objectEntry = obj->getEntry();

                if (entry != 0 && objectEntry != entry)
                    return;

                const float dx = slot.pos.x - centerPos.x;
                const float dy = slot.pos.y - centerPos.y;
                if ((dx * dx + dy * dy) <= r2)
                    out.push_back(obj);
            });
        }

        template<typename T>
        void collectObjectsInRange(const LocationVector& centerPos, float maxSearchRange, std::vector<T*>& out) const
        {
            if (maxSearchRange < 0.0f)
                return;

            const float r2 = maxSearchRange * maxSearchRange;

            if constexpr (std::is_same_v<T, Object>)
            {
                forEachAnyObjectInRange(centerPos, maxSearchRange, [&](ObjectHandle h, const ObjectSlot& slot)
                {
                    if (!slot.native)
                        return;

                    const float dx = slot.pos.x - centerPos.x;
                    const float dy = slot.pos.y - centerPos.y;
                    if ((dx * dx + dy * dy) <= r2)
                        out.push_back(static_cast<Object*>(slot.native));
                });
            }
            else
            {
                forEachContainerObjectInRange<T>(centerPos, maxSearchRange, [&](ObjectHandle h, const ObjectSlot& slot)
                {
                    if (!slot.native)
                        return;

                    const float dx = slot.pos.x - centerPos.x;
                    const float dy = slot.pos.y - centerPos.y;
                    if ((dx * dx + dy * dy) <= r2)
                        out.push_back(static_cast<T*>(slot.native));
                });
            }
        }

        void collectUnitsInRange(const LocationVector& centerPos, float maxSearchRange, std::vector<Unit*>& out) const
        {
            if (maxSearchRange < 0.0f)
                return;

            const float r2 = maxSearchRange * maxSearchRange;
            constexpr std::array<std::size_t, 3> unitContainers{
                static_cast<std::size_t>(Container::Players),
                static_cast<std::size_t>(Container::Creatures),
                static_cast<std::size_t>(Container::Pets)
            };

            forEachObjectInContainers(centerPos, maxSearchRange, unitContainers, [&](ObjectHandle h, const ObjectSlot& slot)
            {
                if (!slot.native)
                    return;

                const float dx = slot.pos.x - centerPos.x;
                const float dy = slot.pos.y - centerPos.y;
                if ((dx * dx + dy * dy) <= r2)
                    out.push_back(static_cast<Unit*>(slot.native));
            });
        }

        template<typename T>
        T* findNearestByEntry(const LocationVector& centerPos, std::uint32_t entry, float maxSearchRange) const
        {
            if (maxSearchRange < 0.0f)
                return nullptr;

            const float r2 = maxSearchRange * maxSearchRange;
            T* best = nullptr;
            float bestD2 = std::numeric_limits<float>::max();

            forEachContainerObjectInRange<T>(centerPos, maxSearchRange, [&](ObjectHandle h, const ObjectSlot& slot)
            {
                T* obj = static_cast<T*>(slot.native);
                if (!obj)
                    return;

                std::uint32_t objectEntry = 0;
                if constexpr (requires (T* t) { t->GetEntry(); })
                    objectEntry = obj->GetEntry();
                else if constexpr (requires (T* t) { t->getEntry(); })
                    objectEntry = obj->getEntry();

                if (entry != 0 && objectEntry != entry)
                    return;

                const float dx = slot.pos.x - centerPos.x;
                const float dy = slot.pos.y - centerPos.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 <= r2 && d2 < bestD2)
                {
                    bestD2 = d2;
                    best = obj;
                }
            });

            return best;
        }

        std::size_t gridCount() const noexcept { return grids_.size(); }

        GridMap& grids() noexcept { return grids_; }
        const GridMap& grids() const noexcept { return grids_; }

    private:
        void attachToCell(ObjectHandle h, const LocationVector& pos)
        {
            const auto& meta = pool_.meta(h);
            const int gid = packGridFromPos(pos);
            const int lcid = packCellFromPos(pos);

            auto& grid = getOrCreateGrid(gid);
            auto& cell = getOrCreateCell(grid, lcid);

            {
                std::unique_lock<std::shared_mutex> gl(grid.mtx);
                grid.owners[static_cast<std::size_t>(meta.container)].push_back(h);
            }

            {
                std::unique_lock<std::shared_mutex> cl(cell.mtx);
                cell.byContainer[static_cast<std::size_t>(meta.container)].push_back(h);
                cell.epoch.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void detachFromCell(ObjectHandle h, const LocationVector& pos)
        {
            const auto& meta = pool_.meta(h);
            const int gid = packGridFromPos(pos);
            const int lcid = packCellFromPos(pos);

            auto* grid = tryGetGrid(gid);
            if (!grid)
                return;

            {
                std::unique_lock<std::shared_mutex> gl(grid->mtx);
                auto& owners = const_cast<std::array<std::vector<ObjectHandle>, static_cast<std::size_t>(Container::Count)>&>(grid->owners)
                    [static_cast<std::size_t>(meta.container)];
                owners.erase(std::remove(owners.begin(), owners.end(), h), owners.end());
            }

            if (auto* cell = tryGetCell(*grid, lcid))
            {
                std::unique_lock<std::shared_mutex> cl(cell->mtx);
                auto& bucket = const_cast<std::array<std::vector<ObjectHandle>, static_cast<std::size_t>(Container::Count)>&>(cell->byContainer)
                    [static_cast<std::size_t>(meta.container)];
                bucket.erase(std::remove(bucket.begin(), bucket.end(), h), bucket.end());
                cell->epoch.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void reindexWithinGrid(ObjectHandle h, int gid, int oldLcid, int newLcid)
        {
            const auto& meta = pool_.meta(h);
            auto* grid = tryGetGrid(gid);
            if (!grid)
                return;

            if (auto* oldCell = tryGetCell(*grid, oldLcid))
            {
                std::unique_lock<std::shared_mutex> lk(oldCell->mtx);
                auto& bucket = const_cast<std::array<std::vector<ObjectHandle>, static_cast<std::size_t>(Container::Count)>&>(oldCell->byContainer)
                    [static_cast<std::size_t>(meta.container)];
                bucket.erase(std::remove(bucket.begin(), bucket.end(), h), bucket.end());
                oldCell->epoch.fetch_add(1, std::memory_order_relaxed);
            }

            auto& newCell = getOrCreateCell(const_cast<GridChunk&>(*grid), newLcid);
            {
                std::unique_lock<std::shared_mutex> lk(newCell.mtx);
                auto& bucket = newCell.byContainer[static_cast<std::size_t>(meta.container)];
                if (std::find(bucket.begin(), bucket.end(), h) == bucket.end())
                    bucket.push_back(h);
                newCell.epoch.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void transferCell(ObjectHandle h, int oldGid, int newGid, int oldLcid, int newLcid)
        {
            const auto& meta = pool_.meta(h);

            if (auto* oldGrid = tryGetGrid(oldGid))
            {
                {
                    std::unique_lock<std::shared_mutex> gl(oldGrid->mtx);
                    auto& owners = const_cast<std::array<std::vector<ObjectHandle>, static_cast<std::size_t>(Container::Count)>&>(oldGrid->owners)
                        [static_cast<std::size_t>(meta.container)];
                    owners.erase(std::remove(owners.begin(), owners.end(), h), owners.end());
                }

                if (auto* oldCell = tryGetCell(*oldGrid, oldLcid))
                {
                    std::unique_lock<std::shared_mutex> lk(oldCell->mtx);
                    auto& bucket = const_cast<std::array<std::vector<ObjectHandle>, static_cast<std::size_t>(Container::Count)>&>(oldCell->byContainer)
                        [static_cast<std::size_t>(meta.container)];
                    bucket.erase(std::remove(bucket.begin(), bucket.end(), h), bucket.end());
                    oldCell->epoch.fetch_add(1, std::memory_order_relaxed);
                }
            }

            auto& newGrid = getOrCreateGrid(newGid);
            {
                std::unique_lock<std::shared_mutex> gl(newGrid.mtx);
                auto& owners = newGrid.owners[static_cast<std::size_t>(meta.container)];
                if (std::find(owners.begin(), owners.end(), h) == owners.end())
                    owners.push_back(h);
            }

            auto& newCell = getOrCreateCell(newGrid, newLcid);
            {
                std::unique_lock<std::shared_mutex> lk(newCell.mtx);
                auto& bucket = newCell.byContainer[static_cast<std::size_t>(meta.container)];
                if (std::find(bucket.begin(), bucket.end(), h) == bucket.end())
                    bucket.push_back(h);
                newCell.epoch.fetch_add(1, std::memory_order_relaxed);
            }
        }


        template<std::size_t N, typename Fn>
        void forEachObjectInContainers(const LocationVector& centerPos, float maxSearchRange, const std::array<std::size_t, N>& containers, Fn&& fn) const
        {
            const float minX = centerPos.x - maxSearchRange;
            const float maxX = centerPos.x + maxSearchRange;
            const float minY = centerPos.y - maxSearchRange;
            const float maxY = centerPos.y + maxSearchRange;

            auto clampGrid = [](int g) { return std::clamp(g, 0, Terrain::TilesCount - 1); };
            const int minGx = clampGrid(static_cast<int>(std::floor((minX - Terrain::MinX) / Terrain::TileSize)));
            const int maxGx = clampGrid(static_cast<int>(std::floor((maxX - Terrain::MinX) / Terrain::TileSize)));
            const int minGy = clampGrid(static_cast<int>(std::floor((minY - Terrain::MinY) / Terrain::TileSize)));
            const int maxGy = clampGrid(static_cast<int>(std::floor((maxY - Terrain::MinY) / Terrain::TileSize)));

            for (int gy = minGy; gy <= maxGy; ++gy)
            {
                for (int gx = minGx; gx <= maxGx; ++gx)
                {
                    const float gridOriginX = Terrain::MinX + gx * Terrain::TileSize;
                    const float gridOriginY = Terrain::MinY + gy * Terrain::TileSize;

                    auto clampCell = [](int c) { return std::clamp(c, 0, Cell::CellsPerTile - 1); };
                    const int minCx = clampCell(static_cast<int>(std::floor((minX - gridOriginX) / Cell::Size)));
                    const int maxCx = clampCell(static_cast<int>(std::floor((maxX - gridOriginX) / Cell::Size)));
                    const int minCy = clampCell(static_cast<int>(std::floor((minY - gridOriginY) / Cell::Size)));
                    const int maxCy = clampCell(static_cast<int>(std::floor((maxY - gridOriginY) / Cell::Size)));

                    const int gid = packGridId(gx, gy);
                    const GridChunk* grid = tryGetGrid(gid);
                    if (!grid)
                        continue;

                    for (int cy = minCy; cy <= maxCy; ++cy)
                    {
                        for (int cx = minCx; cx <= maxCx; ++cx)
                        {
                            const int lcid = packCellId(cx, cy);
                            const CellChunk* cell = tryGetCell(*grid, lcid);
                            if (!cell)
                                continue;

                            std::shared_lock<std::shared_mutex> lk(cell->mtx);
                            for (std::size_t idx : containers)
                            {
                                const auto& list = cell->byContainer[idx];
                                for (auto h : list)
                                {
                                    const ObjectSlot* slot = nullptr;
                                    if (!tryGet(h, slot) || !slot || !slot->native)
                                        continue;

                                    fn(h, *slot);
                                }
                            }
                        }
                    }
                }
            }
        }

        template<typename T, typename Fn>
        void forEachContainerObjectInRange(const LocationVector& centerPos, float maxSearchRange, Fn&& fn) const
        {
            constexpr std::array<std::size_t, 1> containers{ static_cast<std::size_t>(TypeMap<T>::container) };
            forEachObjectInContainers(centerPos, maxSearchRange, containers, std::forward<Fn>(fn));
        }

        template<typename Fn>
        void forEachAnyObjectInRange(const LocationVector& centerPos, float maxSearchRange, Fn&& fn) const
        {
            std::array<std::size_t, static_cast<std::size_t>(Container::Count)> containers{};
            for (std::size_t i = 0; i < containers.size(); ++i)
                containers[i] = i;

            const float minX = centerPos.x - maxSearchRange;
            const float maxX = centerPos.x + maxSearchRange;
            const float minY = centerPos.y - maxSearchRange;
            const float maxY = centerPos.y + maxSearchRange;

            auto clampGrid = [](int g) { return std::clamp(g, 0, Terrain::TilesCount - 1); };
            const int minGx = clampGrid(static_cast<int>(std::floor((minX - Terrain::MinX) / Terrain::TileSize)));
            const int maxGx = clampGrid(static_cast<int>(std::floor((maxX - Terrain::MinX) / Terrain::TileSize)));
            const int minGy = clampGrid(static_cast<int>(std::floor((minY - Terrain::MinY) / Terrain::TileSize)));
            const int maxGy = clampGrid(static_cast<int>(std::floor((maxY - Terrain::MinY) / Terrain::TileSize)));

            for (int gy = minGy; gy <= maxGy; ++gy)
            {
                for (int gx = minGx; gx <= maxGx; ++gx)
                {
                    const float gridOriginX = Terrain::MinX + gx * Terrain::TileSize;
                    const float gridOriginY = Terrain::MinY + gy * Terrain::TileSize;

                    auto clampCell = [](int c) { return std::clamp(c, 0, Cell::CellsPerTile - 1); };
                    const int minCx = clampCell(static_cast<int>(std::floor((minX - gridOriginX) / Cell::Size)));
                    const int maxCx = clampCell(static_cast<int>(std::floor((maxX - gridOriginX) / Cell::Size)));
                    const int minCy = clampCell(static_cast<int>(std::floor((minY - gridOriginY) / Cell::Size)));
                    const int maxCy = clampCell(static_cast<int>(std::floor((maxY - gridOriginY) / Cell::Size)));

                    const int gid = packGridId(gx, gy);
                    const GridChunk* grid = tryGetGrid(gid);
                    if (!grid)
                        continue;

                    for (int cy = minCy; cy <= maxCy; ++cy)
                    {
                        for (int cx = minCx; cx <= maxCx; ++cx)
                        {
                            const int lcid = packCellId(cx, cy);
                            const CellChunk* cell = tryGetCell(*grid, lcid);
                            if (!cell)
                                continue;

                            std::shared_lock<std::shared_mutex> lk(cell->mtx);
                            for (std::size_t idx = 0; idx < static_cast<std::size_t>(Container::Count); ++idx)
                            {
                                const auto& list = cell->byContainer[idx];
                                for (auto h : list)
                                {
                                    const ObjectSlot* slot = nullptr;
                                    if (!tryGet(h, slot) || !slot || !slot->native)
                                        continue;

                                    fn(h, *slot);
                                }
                            }
                        }
                    }
                }
            }
        }

        ObjectPool pool_;
        GridMap grids_;
        mutable std::shared_mutex guidMtx_;
        std::unordered_map<std::uint64_t, ObjectHandle> guidToHandle_;
    };
}
