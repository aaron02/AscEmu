/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include <algorithm>
#include <mutex>
#include <variant>
#include <shared_mutex>
#include <type_traits>
#include <unordered_set>
#include <set>
#include <future>

#include "VisibilitySystem.hpp"
#include "CommandQueue.hpp"

#include "Objects/Object.hpp"
#include "Objects/Units/Unit.hpp"
#include "Objects/Units/Creatures/Creature.h"
#include "Objects/GameObject.h"
#include "Objects/DynamicObject.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Objects/Units/Creatures/Pet.h"
#include "Objects/Units/Creatures/Corpse.hpp"

//////////////////////////////////////////////////////////////////////////////////////////
/// visibility
//////////////////////////////////////////////////////////////////////////////////////////
namespace visibility
{
    namespace
    {
        bool gridHasPhysicalObjects(const GridChunk& g)
        {
            std::shared_lock<std::shared_mutex> lk(g.mtx);
            for (const auto& owners : g.owners)
            {
                if (!owners.empty())
                    return true;
            }
            return false;
        }

        bool gridHasSubscriptions(const GridChunk& g)
        {
            std::shared_lock<std::shared_mutex> glk(g.mtx);
            for (const auto& [lcid, cell] : g.cells)
            {
                std::shared_lock<std::shared_mutex> clk(cell.mtx);
                if (!cell.viewers.empty() || !cell.activators.empty())
                    return true;
            }

            return false;
        }

        bool gridIsCompletelyEmpty(const GridChunk& g)
        {
            return g.activeCells.load(std::memory_order_relaxed) <= 0
                && !gridHasPhysicalObjects(g)
                && !gridHasSubscriptions(g);
        }

        void addUnique(std::vector<ObjectHandle>& vec, ObjectHandle h)
        {
            if (std::find(vec.begin(), vec.end(), h) == vec.end())
                vec.push_back(h);
        }

        bool isPublishActive(const PubState& pub) noexcept
        {
            return pub.mode != PublishMode::None;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// ctor
    ////////////////////////////////////////////////////////////////////////////////////////
    VisibilitySystem::VisibilitySystem(SpatialIndex& spatialIndex, Config cfg, std::size_t queueCap)
        : cfg_(cfg), spatialIndex_(spatialIndex), cmdQueue_(queueCap)
    {
    }

    std::uint64_t VisibilitySystem::nextCandidateQueryStamp() const
    {
        ++candidateQueryStamp_;
        if (candidateQueryStamp_ == 0)
            candidateQueryStamp_ = 1;

        return candidateQueryStamp_;
    }

    bool VisibilitySystem::markCandidateOnce(ObjectHandle h, std::uint64_t stamp) const
    {
        const ObjectSlot* slot = nullptr;
        if (!spatialIndex_.tryGet(h, slot))
            return false;

        if (slot->visibilityQueryStamp == stamp)
            return false;

        slot->visibilityQueryStamp = stamp;
        return true;
    }

    void VisibilitySystem::hideObjectFromKnownViewers(const WoWGuid& objectGuid)
    {
        const uint64_t objectRaw = objectGuid.getRawGuid();
        if (!objectRaw)
            return;

        std::vector<WoWGuid> viewers;
        {
            std::shared_lock<std::shared_mutex> lk(visibleNowMtx_);
            auto it = seenBy_.find(objectRaw);
            if (it != seenBy_.end())
            {
                viewers.reserve(it->second.size());
                for (uint64_t viewerRaw : it->second)
                    viewers.emplace_back(viewerRaw);
            }
        }

        for (const WoWGuid& viewerGuid : viewers)
            emitHiddenOnce(viewerGuid, objectGuid);
    }

    bool VisibilitySystem::isWithinInterestReach(const ObjectSlot& viewer, const ObjectSlot& obj, int objGid, int objLcid) const
    {
        if (!viewer.sub.active || !viewer.sub.viewer)
            return false;

        const int d = cellChebDistGlobal(viewer.sub.gid, viewer.sub.lcx, viewer.sub.lcy, objGid, objLcid);

        // Normal subscription ring.
        if (d <= viewer.sub.radius)
            return true;

        if (!isPublishActive(obj.pub))
            return false;

        if (obj.pub.playersOnly && viewer.meta.container != Container::Players)
            return false;

        if (obj.pub.mode == PublishMode::GridWide)
            return d <= cfg_.gridWidePublishCells;

        // CellRadius is treated as a total announce radius, not as an additive
        // extension over the viewer ring. This keeps rules such as player->player
        // 4 cells deterministic regardless of the configured default viewer ring.
        if (obj.pub.mode == PublishMode::CellRadius)
            return obj.pub.extraCells > 0 && d <= std::max(viewer.sub.radius, obj.pub.extraCells);

        return false;
    }

    /// Check whether publish-range alone keeps an object visible to a viewer.
    bool VisibilitySystem::isPublishedVisibleFor(const ObjectSlot* obj, const ObjectSlot* viewer, int objGid, int objLcid) const
    {
        if (!obj || !viewer)
            return false;

        const int d = cellChebDistGlobal(viewer->sub.gid, viewer->sub.lcx, viewer->sub.lcy, objGid, objLcid);
        if (d <= viewer->sub.radius)
            return false;

        return isWithinInterestReach(*viewer, *obj, objGid, objLcid);
    }

    /// Hide all objects in given cells for a viewer unless publish keeps them visible
    void VisibilitySystem::hideObjectsInCellsForViewer(ObjectHandle viewerH, const std::vector<CellRef>& cells)
    {
        const ObjectSlot* viewer = nullptr;

        if (!spatialIndex_.tryGet(viewerH, viewer) || !viewer->sub.active || !viewer->sub.viewer)
            return;

        const auto stamp = nextCandidateQueryStamp();

        for (auto [gid, lcid] : cells)
        {
            const GridChunk* gp = spatialIndex_.tryGetGrid(gid);
            if (!gp)
                continue;
            const CellChunk* cp = spatialIndex_.tryGetCell(*gp, lcid);
            if (!cp)
                continue;

            std::shared_lock lk(cp->mtx);
            for (size_t ci = 0; ci < static_cast<size_t>(Container::Count); ++ci)
            {
                for (auto oh : cp->byContainer[ci])
                {
                    if (!markCandidateOnce(oh, stamp))
                        continue;

                    const ObjectSlot* obj = nullptr;
                    if (!spatialIndex_.tryGet(oh, obj))
                        continue;

                    const int objL = SpatialIndex::packCellFromPos(obj->pos);
                    const int objG = SpatialIndex::packGridFromPos(obj->pos);

                    if (isPublishedVisibleFor(obj, viewer, objG, objL))
                        continue;

                    emitHiddenOnce(viewer->meta.guid, obj->meta.guid);
                }
            }
        }
    }

    bool VisibilitySystem::isVisibleToViewer(const ObjectSlot* obj, const ObjectSlot* viewer, int objGid, int objLcid) const
    {
        if (!obj || !viewer)
            return false;

        if (!isWithinInterestReach(*viewer, *obj, objGid, objLcid))
            return false;

        auto* vu = static_cast<Unit*>(viewer->native);
        auto* oo = static_cast<Object*>(obj->native);
        return (vu && oo) ? vu->canSee(oo) : false;
    }

    bool VisibilitySystem::shouldRefreshViewerInterest(const ObjectSlot& viewer, const SpatialMoveResult& move) const
    {
        if (move.cellChanged)
            return true;

        if (!viewer.sub.lastInterestRefreshValid)
            return true;

        const float dx = move.newPos.x - viewer.sub.lastInterestRefreshPos.x;
        const float dy = move.newPos.y - viewer.sub.lastInterestRefreshPos.y;
        const float threshold = std::max(0.0f, cfg_.viewerInterestRefreshDistance);
        return (dx * dx + dy * dy) >= (threshold * threshold);
    }

    void VisibilitySystem::markViewerInterestRefreshed(ObjectSlot& viewer, const LocationVector& pos)
    {
        viewer.sub.lastInterestRefreshPos = pos;
        viewer.sub.lastInterestRefreshValid = true;
    }

    void VisibilitySystem::refreshViewerInterest(ObjectHandle viewerH, ObjectSlot& viewer)
    {
        if (!viewer.sub.viewer)
            return;

        proximitySweepViewer(viewerH);
        emitPublishedForViewer(viewerH, /*isSubscribe*/true);
        markViewerInterestRefreshed(viewer, viewer.pos);
    }

    bool VisibilitySystem::shouldRefreshMovedObjectInterest(const ObjectSlot& object, const SpatialMoveResult& move) const
    {
        if (move.cellChanged)
            return true;

        if (!object.lastObjectInterestRefreshValid)
            return true;

        const float dx = move.newPos.x - object.lastObjectInterestRefreshPos.x;
        const float dy = move.newPos.y - object.lastObjectInterestRefreshPos.y;
        const float threshold = std::max(0.0f, cfg_.movedObjectInterestRefreshDistance);
        return (dx * dx + dy * dy) >= (threshold * threshold);
    }

    void VisibilitySystem::markMovedObjectInterestRefreshed(ObjectSlot& object, const LocationVector& pos)
    {
        object.lastObjectInterestRefreshPos = pos;
        object.lastObjectInterestRefreshValid = true;
    }

    void VisibilitySystem::refreshMovedObjectInterest(ObjectHandle objectH, ObjectSlot& object)
    {
        std::vector<ObjectHandle> viewers;
        viewers.reserve(256);
        collectCandidateViewersForObject(objectH, viewers);

        for (auto viewerH : viewers)
            updateVisibilityPair(viewerH, objectH);

        markMovedObjectInterestRefreshed(object, object.pos);
    }

    bool VisibilitySystem::shouldViewerSeeObject(ObjectHandle viewerH, ObjectHandle objH) const
    {
        const ObjectSlot* viewer = nullptr;
        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(viewerH, viewer) || !spatialIndex_.tryGet(objH, obj))
            return false;

        const int objGid = SpatialIndex::packGridFromPos(obj->pos);
        const int objLcid = SpatialIndex::packCellFromPos(obj->pos);
        return isVisibleToViewer(obj, viewer, objGid, objLcid);
    }

    void VisibilitySystem::updateVisibilityPair(ObjectHandle viewerH, ObjectHandle objH)
    {
        const ObjectSlot* viewer = nullptr;
        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(viewerH, viewer) || !spatialIndex_.tryGet(objH, obj))
            return;

        if (!viewer->sub.active || !viewer->sub.viewer)
            return;

        if (shouldViewerSeeObject(viewerH, objH))
            emitVisibleOnce(viewer->meta.guid, obj->meta.guid);
        else
            emitHiddenOnce(viewer->meta.guid, obj->meta.guid);
    }

    void VisibilitySystem::collectCandidateViewersForObject(ObjectHandle objH, std::vector<ObjectHandle>& out) const
    {
        out.clear();

        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(objH, obj))
            return;

        const int objGid = SpatialIndex::packGridFromPos(obj->pos);
        const int objLcid = SpatialIndex::packCellFromPos(obj->pos);
        auto [objLcx, objLcy] = unpackCellId2(objLcid);

        const auto stamp = nextCandidateQueryStamp();

        auto addViewer = [&](ObjectHandle vh)
        {
            if (!markCandidateOnce(vh, stamp))
                return;

            const ObjectSlot* viewer = nullptr;
            if (!spatialIndex_.tryGet(vh, viewer))
                return;

            if (!viewer->sub.active || !viewer->sub.viewer)
                return;

            if (isWithinInterestReach(*viewer, *obj, objGid, objLcid))
                out.push_back(vh);
        };

        int candidateRadius = 0;
        if (isPublishActive(obj->pub))
        {
            if (obj->pub.mode == PublishMode::GridWide)
                candidateRadius = std::max(candidateRadius, cfg_.gridWidePublishCells);
            else if (obj->pub.mode == PublishMode::CellRadius)
                candidateRadius = std::max(candidateRadius, obj->pub.extraCells);
        }

        // Normal objects only need the object's current cell because viewer rings
        // are materialized as cell viewers. Published objects need a wider
        // candidate scan so stationary publishers can announce to later viewers.
        if (candidateRadius <= 0)
        {
            if (const GridChunk* gp = spatialIndex_.tryGetGrid(objGid))
            {
                if (const CellChunk* cp = spatialIndex_.tryGetCell(*gp, objLcid))
                {
                    std::shared_lock<std::shared_mutex> lk(cp->mtx);
                    for (auto vh : cp->viewers)
                        addViewer(vh);
                }
            }
            return;
        }

        const auto cells = spatialIndex_.buildRingCells(objGid, objLcx, objLcy, candidateRadius);
        for (auto [gid, lcid] : cells)
        {
            const GridChunk* gp = spatialIndex_.tryGetGrid(gid);
            if (!gp)
                continue;
            const CellChunk* cp = spatialIndex_.tryGetCell(*gp, lcid);
            if (!cp)
                continue;

            std::shared_lock<std::shared_mutex> lk(cp->mtx);
            for (auto vh : cp->viewers)
                addViewer(vh);
        }
    }

    bool VisibilitySystem::isPlayerSpatialPublisher(const ObjectSlot& obj) const
    {
        return obj.meta.container == Container::Players &&
            obj.pub.mode == PublishMode::CellRadius &&
            obj.pub.playersOnly &&
            obj.pub.extraCells > 0;
    }

    bool VisibilitySystem::isBucketedPublisher(const ObjectSlot& obj) const
    {
        if (!isPublishActive(obj.pub))
            return false;

        // Player->Player publishing is resolved through the SpatialIndex around
        // the viewer/object. Keeping every player in a global publisher list would
        // make viewer refreshes scale with total map population.
        return !isPlayerSpatialPublisher(obj);
    }

    void VisibilitySystem::addPublishBucket(ObjectHandle who, const ObjectSlot& obj)
    {
        if (!isBucketedPublisher(obj))
            return;

        if (obj.pub.mode == PublishMode::GridWide)
        {
            addUnique(gridWidePublishers_, who);
            return;
        }

        if (obj.pub.mode == PublishMode::CellRadius)
            addUnique(cellRadiusPublishers_, who);
    }

    void VisibilitySystem::removePublishBucket(ObjectHandle who)
    {
        auto removeFrom = [&](std::vector<ObjectHandle>& v, const char* name)
        {
            const auto before = std::count(v.begin(), v.end(), who);
            if (before > 1)
                sLogger.warning("vis sanity: duplicate handle in {} publish bucket id={} gen={} count={}", name, who.id, who.gen, before);

            v.erase(std::remove(v.begin(), v.end(), who), v.end());

            if (std::find(v.begin(), v.end(), who) != v.end())
                sLogger.warning("vis sanity: handle remained in {} publish bucket after removal id={} gen={}", name, who.id, who.gen);
        };

        removeFrom(gridWidePublishers_, "gridWide");
        removeFrom(cellRadiusPublishers_, "cellRadius");
    }

    bool VisibilitySystem::isInPublishBucket(ObjectHandle who) const
    {
        return std::find(gridWidePublishers_.begin(), gridWidePublishers_.end(), who) != gridWidePublishers_.end()
            || std::find(cellRadiusPublishers_.begin(), cellRadiusPublishers_.end(), who) != cellRadiusPublishers_.end();
    }

    bool VisibilitySystem::hasCellSubscription(ObjectHandle who) const
    {
        for (const auto& [gid, g] : spatialIndex_.grids())
        {
            std::shared_lock<std::shared_mutex> glk(g.mtx);
            for (const auto& [lcid, cell] : g.cells)
            {
                std::shared_lock<std::shared_mutex> clk(cell.mtx);
                if (std::find(cell.viewers.begin(), cell.viewers.end(), who) != cell.viewers.end())
                    return true;
                if (std::find(cell.activators.begin(), cell.activators.end(), who) != cell.activators.end())
                    return true;
            }
        }
        return false;
    }

    void VisibilitySystem::collectPlayerPublishersAroundCell(int gid, int lcx, int lcy, int radiusCells,
        std::vector<ObjectHandle>& out, std::uint64_t stamp) const
    {
        if (radiusCells <= 0)
            return;

        const auto cells = spatialIndex_.buildRingCells(gid, lcx, lcy, radiusCells);
        for (auto [cgid, clcid] : cells)
        {
            const GridChunk* gp = spatialIndex_.tryGetGrid(cgid);
            if (!gp)
                continue;

            const CellChunk* cp = spatialIndex_.tryGetCell(*gp, clcid);
            if (!cp)
                continue;

            std::shared_lock<std::shared_mutex> lk(cp->mtx);
            const auto& players = cp->byContainer[static_cast<std::size_t>(Container::Players)];
            for (auto oh : players)
            {
                if (markCandidateOnce(oh, stamp))
                    out.push_back(oh);
            }
        }
    }

    void VisibilitySystem::reconcilePublishedCandidateForViewerMove(ObjectHandle viewerH, const ObjectSlot& viewer, ObjectHandle objH,
        int oldGid, int oldLcid, int newGid, int newLcid)
    {
        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(objH, obj))
            return;
        if (!isPublishActive(obj->pub))
            return;
        if (obj->pub.playersOnly && viewer.meta.container != Container::Players)
            return;

        auto [oldLcx, oldLcy] = unpackCellId2(oldLcid);
        auto [newLcx, newLcy] = unpackCellId2(newLcid);

        const int objG = SpatialIndex::packGridFromPos(obj->pos);
        const int objL = SpatialIndex::packCellFromPos(obj->pos);

        const int oldD = cellChebDistGlobal(oldGid, oldLcx, oldLcy, objG, objL);
        const int newD = cellChebDistGlobal(newGid, newLcx, newLcy, objG, objL);

        const bool oldRing = oldD <= viewer.sub.radius;
        const bool newRing = newD <= viewer.sub.radius;
        // isWithinInterestReach uses the viewer's current subscription position.
        // For the old side, reproduce the same reach rule with the old distance.
        bool oldPublished = false;
        if (!oldRing)
        {
            if (obj->pub.mode == PublishMode::GridWide)
                oldPublished = oldD <= cfg_.gridWidePublishCells;
            else if (obj->pub.mode == PublishMode::CellRadius)
                oldPublished = obj->pub.extraCells > 0 && oldD <= std::max(viewer.sub.radius, obj->pub.extraCells);
        }

        bool newPublished = false;
        if (!newRing)
        {
            if (obj->pub.mode == PublishMode::GridWide)
                newPublished = newD <= cfg_.gridWidePublishCells;
            else if (obj->pub.mode == PublishMode::CellRadius)
                newPublished = obj->pub.extraCells > 0 && newD <= std::max(viewer.sub.radius, obj->pub.extraCells);
        }

        if (!oldPublished && newPublished)
        {
            updateVisibilityPair(viewerH, objH);
            return;
        }

        if (oldPublished && !newPublished && !newRing)
        {
            emitHiddenOnce(viewer.meta.guid, obj->meta.guid);
            return;
        }
    }

#ifdef VIS_TRACK_VISIBLE
    ////////////////////////////////////////////////////////////////////////////////////////
    /// Visible tracking helpers
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::collectViewersOf(const WoWGuid& object, std::vector<WoWGuid>& out) const
    {
        std::shared_lock<std::shared_mutex> lk(visibleNowMtx_);
        const uint64_t oraw = object.getRawGuid();
        auto it = seenBy_.find(oraw);
        if (it == seenBy_.end())
            return;
        out.reserve(out.size() + it->second.size());
        for (uint64_t vraw : it->second)
            out.emplace_back(WoWGuid(vraw));
    }
#endif

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Metrics
    ////////////////////////////////////////////////////////////////////////////////////////
    VisSnapshot VisibilitySystem::snapshot() const
    {
        VisSnapshot s;
        s.grids = spatialIndex_.gridCount();
        s.poolLive = spatialIndex_.liveCount();
        s.guidIndex = spatialIndex_.guidCount();
        s.gridWidePublishers = gridWidePublishers_.size();
        s.cellRadiusPublishers = cellRadiusPublishers_.size();

        for (auto const& [gid, g] : spatialIndex_.grids())
        {
            std::shared_lock gl(g.mtx);
            s.cells += g.cells.size();

            const int active = g.activeCells.load(std::memory_order_acquire);
            if (active > 0)
            {
                ++s.activeGrids;
                s.activeCells += static_cast<size_t>(active);
            }

            for (size_t i = 0; i < s.owners.size(); ++i)
            {
                s.owners[i] += g.owners[i].size();
                for (auto h : g.owners[i])
                {
                    const ObjectSlot* slot = nullptr;
                    if (!spatialIndex_.tryGet(h, slot))
                        continue;

                    s.nearCacheGuidsCapacity += slot->nearCache.guids.capacity();
                    s.nearCacheStampsCapacity += slot->nearCache.stamps.capacity();
                    s.nearCacheCachedGuids += slot->nearCache.guids.size();
                }
            }

            for (auto const& [lcid, cell] : g.cells)
            {
                std::shared_lock cl(cell.mtx);
                s.cellViewers += cell.viewers.size();
                s.cellActivators += cell.activators.size();
            }
        }

#ifdef VIS_TRACK_VISIBLE
        {
            std::shared_lock<std::shared_mutex> lk(visibleNowMtx_);
            s.visibleViewers = visibleNow_.size();
            for (auto const& [viewer, objects] : visibleNow_)
                s.visiblePairs += objects.size();

            s.seenObjects = seenBy_.size();
            for (auto const& [object, viewers] : seenBy_)
                s.seenPairs += viewers.size();
        }
#endif

        return s;
    }

    void VisibilitySystem::logMemoryDiagnostics(const char* reason) const
    {
        const auto s = snapshot();
        const auto idx = [](Container c) { return static_cast<std::size_t>(c); };

        sLogger.warning(
            "vismem: reason={} grids={} activeGrids={} cells={} activeCells={} poolLive={} guidIndex={} "
            "owners[coro={},cre={},dyn={},go={},plr={},trans={},pet={},unk={}] "
            "cellViewers={} cellActivators={} visibleViewers={} visiblePairs={} seenObjects={} seenPairs={} "
            "publishers[gridWide={},cellRadius={}] nearCache[cachedGuids={},guidCap={},stampCap={}]",
            reason ? reason : "",
            s.grids,
            s.activeGrids,
            s.cells,
            s.activeCells,
            s.poolLive,
            s.guidIndex,
            s.owners[idx(Container::Corpses)],
            s.owners[idx(Container::Creatures)],
            s.owners[idx(Container::DynamicObjects)],
            s.owners[idx(Container::GameObjects)],
            s.owners[idx(Container::Players)],
            s.owners[idx(Container::Transporter)],
            s.owners[idx(Container::Pets)],
            s.owners[idx(Container::Unk)],
            s.cellViewers,
            s.cellActivators,
            s.visibleViewers,
            s.visiblePairs,
            s.seenObjects,
            s.seenPairs,
            s.gridWidePublishers,
            s.cellRadiusPublishers,
            s.nearCacheCachedGuids,
            s.nearCacheGuidsCapacity,
            s.nearCacheStampsCapacity);
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Spatial lifecycle event entry points
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::onObjectAdded(ObjectHandle h)
    {
        const ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(h, s))
            return;

        const int gid = SpatialIndex::packGridFromPos(s->pos);
        const int lcid = SpatialIndex::packCellFromPos(s->pos);

        notifySpawn(h, gid, lcid, /*alsoActivators*/false);
    }

    void VisibilitySystem::onObjectRemoving(ObjectHandle h)
    {
        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(h, s))
            return;

        hideObjectFromKnownViewers(s->meta.guid);

        if (s->sub.active)
        {
            const auto cells = spatialIndex_.buildRingCells(s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius);

            if (s->sub.viewer)
            {
                hideObjectsInCellsForViewer(h, cells);
                emitPublishedForViewer(h, /*isSubscribe*/false);
                unsubscribeRing(h, s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius, /*asPlayer*/true);
            }

            if (s->sub.activator)
                unsubscribeRing(h, s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius, /*asPlayer*/false);

            if (hasCellSubscription(h))
                sLogger.warning("vis sanity: handle still subscribed after unsubscribeRing during remove guid={} id={} gen={}", s->meta.guid.getRawGuid(), h.id, h.gen);

            s->sub = {};
        }

        const int gid = SpatialIndex::packGridFromPos(s->pos);
        const int lcid = SpatialIndex::packCellFromPos(s->pos);

        notifyDespawn(h, gid, lcid, /*alsoActivators*/false);
        removePublishBucket(h);

        if (isInPublishBucket(h))
            sLogger.warning("vis sanity: handle still in publish bucket after onObjectRemoving guid={} id={} gen={}", s->meta.guid.getRawGuid(), h.id, h.gen);
    }

    void VisibilitySystem::onObjectMoved(ObjectHandle h, const SpatialMoveResult& move)
    {
        if (!move.valid)
            return;

        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(h, s))
            return;

        if (s->sub.active)
        {
            const auto oldCells = spatialIndex_.buildRingCells(s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius);

            auto [lcx, lcy] = worldToLocal(move.newPos);
            const auto newCells = spatialIndex_.buildRingCells(move.newGrid, lcx, lcy, s->sub.radius);

            std::vector<CellRef> toUnsub;
            toUnsub.reserve(oldCells.size());
            std::vector<CellRef> toSub;
            toSub.reserve(newCells.size());

            std::set_difference(oldCells.begin(), oldCells.end(),
                newCells.begin(), newCells.end(),
                std::back_inserter(toUnsub));
            std::set_difference(newCells.begin(), newCells.end(),
                oldCells.begin(), oldCells.end(),
                std::back_inserter(toSub));

            // Add new cells before removing old ones. This is especially important
            // for activators so a move across cell/grid borders cannot temporarily
            // drop activeCells to zero and cause activate/deactivate flapping.
            for (auto [gid, lcid] : toSub)
            {
                if (s->sub.activator) cellSubscribe(h, gid, lcid, /*asPlayer*/false, /*emitEvents*/false);
                if (s->sub.viewer)    cellSubscribe(h, gid, lcid, /*asPlayer*/true,  /*emitEvents*/false);
            }

            for (auto [gid, lcid] : toUnsub)
            {
                if (s->sub.viewer)    cellUnsubscribe(h, gid, lcid, /*asPlayer*/true,  /*emitEvents*/false);
                if (s->sub.activator) cellUnsubscribe(h, gid, lcid, /*asPlayer*/false, /*emitEvents*/false);
            }

            const int oldG = s->sub.gid;
            const int oldLcx = s->sub.lcx;
            const int oldLcy = s->sub.lcy;
            s->sub.gid = move.newGrid;
            s->sub.lcx = lcx;
            s->sub.lcy = lcy;

            if (s->sub.viewer)
                hideObjectsInCellsForViewer(h, toUnsub);

            if (s->sub.viewer && shouldRefreshViewerInterest(*s, move))
                refreshViewerInterest(h, *s);

            if (s->sub.viewer && move.cellChanged)
                reconcilePublishedForViewerMove(h, oldG, packCellId(oldLcx, oldLcy), move.newGrid, packCellId(lcx, lcy));
        }

        if (move.gridChanged)
        {
            for (auto& f : hub_.onGridChanged)
                f(spatialIndex_.meta(h).guid, move.oldGrid, move.newGrid);
        }

        if (move.cellChanged)
        {
            notifyCrossCellMove(h, move.oldGrid, move.oldCell, move.newGrid, move.newCell);
            emitPublishedForPublisherMove(h, move.oldGrid, move.oldCell, move.newGrid, move.newCell);
        }

        if (shouldRefreshMovedObjectInterest(*s, move))
            refreshMovedObjectInterest(h, *s);
    }

    void VisibilitySystem::drain(std::size_t max)
    {
        std::size_t drained = 0;
        WorldCmd cmd;
        int idle = 0;

        while (drained < max)
        {
            if (cmdQueue_.Pop(cmd))
            {
                apply(cmd);
                if (cmd.fence)
                    cmd.fence->set_value();
                ++drained;
                idle = 0;
            }
            else
            {
                if (++idle > 2)
                    break;
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Subscriptions (public API)
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::subscribeViewer(ObjectHandle who, int radiusCells)
    {
        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        WorldCmd c;
        c.payload = CmdSub{ who, radiusCells, true };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: subscribeViewer dropped because command queue is full for guid={}", s->meta.guid.getRawGuid());
    }

    void VisibilitySystem::subscribeActivator(ObjectHandle who, int radiusCells)
    {
        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        WorldCmd c;
        c.payload = CmdSub{ who, radiusCells, false };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: subscribeActivator dropped because command queue is full for guid={}", s->meta.guid.getRawGuid());
    }

    void VisibilitySystem::unsubscribe(ObjectHandle who)
    {
        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        WorldCmd c;
        c.payload = CmdUnsub{ who };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: unsubscribe dropped because command queue is full for guid={}", s->meta.guid.getRawGuid());
    }

    void VisibilitySystem::setViewerRole(ObjectHandle who, bool enabled, int radiusCells)
    {
        if (enabled)
        {
            subscribeViewer(who, radiusCells);
            return;
        }

        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        WorldCmd c;
        c.payload = CmdClearRole{ who, true };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: setViewerRole(false) dropped because command queue is full for guid={}", s->meta.guid.getRawGuid());
    }

    void VisibilitySystem::setActivatorRole(ObjectHandle who, bool enabled, int radiusCells)
    {
        if (enabled)
        {
            subscribeActivator(who, radiusCells);
            return;
        }

        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        WorldCmd c;
        c.payload = CmdClearRole{ who, false };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: setActivatorRole(false) dropped because command queue is full for guid={}", s->meta.guid.getRawGuid());
    }

    void VisibilitySystem::activateGrid(int gid)
    {
        WorldCmd c;
        c.payload = CmdActivateGrid{ gid };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: activateGrid dropped because command queue is full for grid={}", gid);
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Interest profiles
    ////////////////////////////////////////////////////////////////////////////////////////
    InterestProfile VisibilitySystem::buildInterestProfile(Object* obj) const
    {
        InterestProfile profile;

        profile.viewerSubscribeCells = cfg_.defaultViewerRadius;
        profile.activatorSubscribeCells = cfg_.defaultActivatorRadius;

        if (!obj)
            return profile;

        if (obj->isPlayer())
        {
            profile.viewer = true;
            profile.activator = true;
            profile.publishMode = PublishMode::CellRadius;
            profile.publishCells = 4;
            profile.publishPlayersOnly = true;
            return profile;
        }

        if (obj->isTransporter())
        {
            profile.activator = true;
            profile.publishMode = PublishMode::GridWide;
            profile.publishCells = 0;
            profile.publishPlayersOnly = true;
            return profile;
        }

        // Future hook: large creatures/gameobjects can extend their announce distance here
        // based on combat reach, display bounds, scale, or custom flags.
        profile.extraVisibilityYards = 0.0f;
        return profile;
    }

    void VisibilitySystem::applyInterestProfile(ObjectHandle who, const InterestProfile& profile)
    {
        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        s->interest = profile;

        if (profile.viewer)
            subscribeViewer(who, std::max(0, profile.viewerSubscribeCells));

        if (profile.activator)
            subscribeActivator(who, std::max(0, profile.activatorSubscribeCells));

        if (profile.publishMode != PublishMode::None &&
            (profile.publishMode == PublishMode::GridWide || profile.publishCells > 0))
        {
            publish(who, profile.publishCells, profile.publishPlayersOnly);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Publishing (public API)
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::publish(ObjectHandle who, int extraCells, bool playersOnly)
    {
        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        WorldCmd c;
        c.payload = CmdPublish{ who, std::max(0, extraCells), playersOnly };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: publish dropped because command queue is full for guid={}", s->meta.guid.getRawGuid());
    }

    void VisibilitySystem::unpublish(ObjectHandle who)
    {
        ObjectSlot* s = nullptr;
        if (!spatialIndex_.tryGet(who, s))
            return;

        WorldCmd c;
        c.payload = CmdUnpublish{ who };
        if (!cmdQueue_.Push(std::move(c)))
            sLogger.warning("vis: unpublish dropped because command queue is full for guid={}", s->meta.guid.getRawGuid());
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Tick
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::tick(std::chrono::milliseconds /*dt*/)
    {
        std::size_t drained = 0;
        WorldCmd cmd;

        while (drained < cfg_.maxCmdsPerTick && cmdQueue_.Pop(cmd))
        {
            apply(cmd);
            if (cmd.fence)
                cmd.fence->set_value();
            ++drained;
        }

        // Idle/unload grids after extended inactivity.
        // This scan is O(gridCount), so run it periodically instead of every map tick.
        auto now = std::chrono::steady_clock::now();
        if (lastGridUnloadScan_.time_since_epoch().count() == 0 ||
            now - lastGridUnloadScan_ >= std::chrono::seconds(1))
        {
            lastGridUnloadScan_ = now;

            for (auto it = spatialIndex_.grids().begin(); it != spatialIndex_.grids().end(); )
            {
                const int gid = it->first;
                auto& g = it->second;
                bool shouldErase = false;

                if (gridIsCompletelyEmpty(g))
                {
                    // Pure structural leftover: no activators, no viewers and no
                    // physical objects. These chunks may be created by temporary
                    // interest rings and do not need the delayed spawn unload path.
                    it = spatialIndex_.grids().erase(it);
                    continue;
                }

                if (g.activeCells.load(std::memory_order_relaxed) <= 0 &&
                    g.idleSince.time_since_epoch().count() != 0 &&
                    now - g.idleSince >= cfg_.cellUnloadDelay)
                {
                    const auto idleFor = now - g.idleSince;

                    // Physical objects do not keep a grid active. They are expected
                    // to be removed by the map/spawn unload callback below.
                    for (auto& cb : hub_.onGridUnload)
                        cb(gid);

                    if (g.activeCells.load(std::memory_order_relaxed) > 0)
                    {
                        // A callback or concurrent command reactivated the grid.
                        g.idleSince = {};
                    }
                    else if (!gridHasPhysicalObjects(g))
                    {
                        shouldErase = true;
                        sLogger.warning("vis: UNLOAD grid={} activeCells=0 owners=0 idleFor={}ms",
                            gid,
                            std::chrono::duration_cast<std::chrono::milliseconds>(idleFor).count());
                    }
                    else
                    {
                        // Unload callback ran but some physical objects still remain.
                        // Keep the grid chunk and retry after the delay instead of
                        // spamming unload callbacks every tick.
                        g.idleSince = now;
                        sLogger.warning("vis: UNLOAD incomplete grid={} activeCells=0 owners=1 idleFor={}ms",
                            gid,
                            std::chrono::duration_cast<std::chrono::milliseconds>(idleFor).count());
                    }
                }

                if (shouldErase)
                    it = spatialIndex_.grids().erase(it);
                else
                    ++it;
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Command application (map thread only)
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::apply(const WorldCmd& cmd)
    {
        auto do_it = [&](auto&& ev)
            {
                using E = std::decay_t<decltype(ev)>;

                if constexpr (std::is_same_v<E, std::monostate>)
                {
                    // no-op
                }
                else if constexpr (std::is_same_v<E, CmdSub>)
                {
                    ObjectSlot* s = nullptr;
                    if (!spatialIndex_.tryGet(ev.who, s))
                        return;

                    const int gid = SpatialIndex::packGridFromPos(s->pos);
                    auto [lcx, lcy] = worldToLocal(s->pos);

                    const bool reqViewer = ev.asPlayer;
                    const bool reqActivator = !ev.asPlayer;

                    if (!s->sub.active)
                    {
                        s->sub = { ev.radius, reqViewer, reqActivator, true, gid, lcx, lcy };
                        const auto cells = spatialIndex_.buildRingCells(gid, lcx, lcy, ev.radius);

                        // Activators first so initial attach cannot briefly create an
                        // inactive grid before the viewer role is fully initialized.
                        if (s->sub.activator)
                        {
                            for (auto [ngid, nlcid] : cells)
                                cellSubscribe(ev.who, ngid, nlcid, /*asPlayer*/false, /*emitEvents*/false);
                        }

                        if (s->sub.viewer)
                        {
                            for (auto [ngid, nlcid] : cells)
                                cellSubscribe(ev.who, ngid, nlcid, /*asPlayer*/true, /*emitEvents*/false);

                            refreshViewerInterest(ev.who, *s);
                        }
                    }
                    else
                    {
                        const bool addViewer = reqViewer && !s->sub.viewer;
                        const bool addActivator = reqActivator && !s->sub.activator;

                        if (ev.radius > s->sub.radius)
                        {
                            const auto oldCells = spatialIndex_.buildRingCells(s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius);
                            const auto newCells = spatialIndex_.buildRingCells(s->sub.gid, s->sub.lcx, s->sub.lcy, ev.radius);

                            std::vector<CellRef> toAdd;
                            std::set_difference(newCells.begin(), newCells.end(),
                                oldCells.begin(), oldCells.end(),
                                std::back_inserter(toAdd));

                            for (auto [ngid, nlcid] : toAdd)
                            {
                                if (s->sub.activator || addActivator) cellSubscribe(ev.who, ngid, nlcid, /*asPlayer*/false, /*emitEvents*/false);
                                if (s->sub.viewer || addViewer)       cellSubscribe(ev.who, ngid, nlcid, /*asPlayer*/true,  /*emitEvents*/false);
                            }

                            s->sub.radius = ev.radius;
                        }

                        if (addActivator || addViewer)
                        {
                            const auto cells = spatialIndex_.buildRingCells(s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius);

                            if (addActivator)
                            {
                                for (auto [ngid, nlcid] : cells)
                                    cellSubscribe(ev.who, ngid, nlcid, /*asPlayer*/false, /*emitEvents*/false);
                            }

                            if (addViewer)
                            {
                                for (auto [ngid, nlcid] : cells)
                                    cellSubscribe(ev.who, ngid, nlcid, /*asPlayer*/true, /*emitEvents*/false);
                            }
                        }

                        s->sub.viewer = s->sub.viewer || reqViewer;
                        s->sub.activator = s->sub.activator || reqActivator;

                        if (s->sub.viewer)
                        {
                            refreshViewerInterest(ev.who, *s);
                        }
                    }
                }
                else if constexpr (std::is_same_v<E, CmdUnsub>)
                {
                    ObjectSlot* s = nullptr;
                    if (!spatialIndex_.tryGet(ev.who, s))
                        return;

                    if (s->sub.active)
                    {
                        const auto cells = spatialIndex_.buildRingCells(s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius);
                        if (s->sub.viewer)
                        {
                            hideObjectsInCellsForViewer(ev.who, cells);     // ring clear (publish-safe)
                            emitPublishedForViewer(ev.who, /*isSubscribe*/false);
                            unsubscribeRing(ev.who, s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius, /*asPlayer*/true);
                        }

                        if (s->sub.activator)
                            unsubscribeRing(ev.who, s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius, /*asPlayer*/false);

                        s->sub.lastInterestRefreshValid = false;
                        s->sub = {};
                    }
                }
                else if constexpr (std::is_same_v<E, CmdClearRole>)
                {
                    ObjectSlot* s = nullptr;
                    if (!spatialIndex_.tryGet(ev.who, s))
                        return;

                    if (!s->sub.active)
                        return;

                    const auto cells = spatialIndex_.buildRingCells(s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius);

                    if (ev.asPlayer)
                    {
                        if (!s->sub.viewer)
                            return;

                        hideObjectsInCellsForViewer(ev.who, cells);
                        emitPublishedForViewer(ev.who, /*isSubscribe*/false);
                        unsubscribeRing(ev.who, s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius, /*asPlayer*/true);
                        s->sub.viewer = false;
                        s->sub.lastInterestRefreshValid = false;
                    }
                    else
                    {
                        if (!s->sub.activator)
                            return;

                        unsubscribeRing(ev.who, s->sub.gid, s->sub.lcx, s->sub.lcy, s->sub.radius, /*asPlayer*/false);
                        s->sub.activator = false;
                    }

                    if (!s->sub.viewer && !s->sub.activator)
                    {
                        s->sub.lastInterestRefreshValid = false;
                        s->sub = {};
                    }
                }
                else if constexpr (std::is_same_v<E, CmdPublish>)
                {
                    ObjectSlot* s = nullptr;
                    if (!spatialIndex_.tryGet(ev.who, s))
                        return;

                    const bool wasActive = isPublishActive(s->pub);
                    s->pub.extraCells = std::max(0, ev.extraCells);
                    s->pub.playersOnly = ev.playersOnly;
                    s->pub.mode = s->interest.publishMode != PublishMode::None ? s->interest.publishMode : PublishMode::CellRadius;

                    // Cell-radius publish needs a positive radius. Grid-wide publish is
                    // active by mode alone and intentionally keeps extraCells at zero.
                    if (s->pub.mode == PublishMode::CellRadius && s->pub.extraCells <= 0)
                        s->pub.mode = PublishMode::None;

                    if (!wasActive && isPublishActive(s->pub))
                    {
                        addPublishBucket(ev.who, *s);
                        const int gid = SpatialIndex::packGridFromPos(s->pos);
                        const int lcid = SpatialIndex::packCellFromPos(s->pos);
                        notifyPublished(ev.who, gid, lcid, /*isSpawn*/true);
                    }
                    else if (wasActive && isPublishActive(s->pub))
                    {
                        // Publishing mode may have changed through an updated profile.
                        removePublishBucket(ev.who);
                        addPublishBucket(ev.who, *s);
                    }
                    else if (wasActive && !isPublishActive(s->pub))
                    {
                        removePublishBucket(ev.who);
                    }
                }
                else if constexpr (std::is_same_v<E, CmdUnpublish>)
                {
                    ObjectSlot* s = nullptr;
                    if (!spatialIndex_.tryGet(ev.who, s))
                        return;

                    if (isPublishActive(s->pub))
                    {
                        const int gid = SpatialIndex::packGridFromPos(s->pos);
                        const int lcid = SpatialIndex::packCellFromPos(s->pos);
                        notifyPublished(ev.who, gid, lcid, /*isSpawn*/false);
                        removePublishBucket(ev.who);
                    }
                    s->pub = {};
                }
                else if constexpr (std::is_same_v<E, CmdActivateGrid>)
                {
                    auto& g = spatialIndex_.getOrCreateGrid(ev.gid);

                    if (g.activeCells.load(std::memory_order_relaxed) > 0)
                        return;

                    const int anyCell = packCellId(0, 0);

                    ObjectHandle ghost{ 0u, 0u };

                    cellSubscribe(ghost, ev.gid, anyCell, /*asPlayer*/false, /*emitEvents*/false);
                    cellUnsubscribe(ghost, ev.gid, anyCell, /*asPlayer*/false, /*emitEvents*/false);
                }
            };
        std::visit(do_it, cmd.payload);
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Subscription helpers (internal)
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::subscribeRing(ObjectHandle who, int baseGid, int lcx, int lcy, int r, bool asPlayer)
    {
        const bool emit = asPlayer; (void)emit; // reserved
        auto cells = spatialIndex_.buildRingCells(baseGid, lcx, lcy, r);
        for (auto [gid, lcid] : cells)
            cellSubscribe(who, gid, lcid, asPlayer, /*emitEvents*/true);
    }

    void VisibilitySystem::unsubscribeRing(ObjectHandle who, int baseGid, int lcx, int lcy, int r, bool asPlayer)
    {
        const bool emit = asPlayer; (void)emit;
        auto cells = spatialIndex_.buildRingCells(baseGid, lcx, lcy, r);
        for (auto [gid, lcid] : cells)
            cellUnsubscribe(who, gid, lcid, asPlayer, /*emitEvents*/true);

#ifdef VIS_TRACK_VISIBLE
        if (asPlayer)
        {
            const ObjectSlot* s = nullptr;
            if (spatialIndex_.tryGet(who, s) && s)
            {
                const uint64_t vraw = s->meta.guid.getRawGuid();
                std::unique_lock<std::shared_mutex> vlk(visibleNowMtx_);
                if (auto it = visibleNow_.find(vraw); it != visibleNow_.end())
                {
                    for (uint64_t oraw : it->second)
                    {
                        if (auto jt = seenBy_.find(oraw); jt != seenBy_.end())
                        {
                            jt->second.erase(vraw);
                            if (jt->second.empty())
                                seenBy_.erase(jt);
                        }
                    }
                    visibleNow_.erase(it);
                }
            }
        }
#endif
    }

    void VisibilitySystem::cellSubscribe(ObjectHandle who, int gid, int lcid, bool asPlayer, bool /*emitEvents*/)
    {
        auto& g = spatialIndex_.getOrCreateGrid(gid);
        auto& cb = spatialIndex_.getOrCreateCell(g, lcid);

        bool activatorsWasEmpty = false;
        bool activatorAdded = false;
        std::size_t nowSize = 0;

        {
            std::unique_lock<std::shared_mutex> lk(cb.mtx);
            auto& list = asPlayer ? cb.viewers : cb.activators;

            if (!asPlayer)
                activatorsWasEmpty = cb.activators.empty();

            const auto oldSize = list.size();
            addUnique(list, who);
            activatorAdded = !asPlayer && list.size() != oldSize;
            nowSize = list.size();

            sLogger.debug("vis: cellSubscribe role={} gid={} lcid={} nowSize={} activeCells(before)={}",
                asPlayer ? "viewer" : "activator", gid, lcid, nowSize,
                (!asPlayer && activatorsWasEmpty) ? g.activeCells.load() : -1);
        }

        if (activatorAdded && activatorsWasEmpty)
        {
            const int before = g.activeCells.fetch_add(1);
            if (before == 0)
            {
                g.idleSince = {};
                for (auto& cbEv : hub_.onGridActivated)
                    cbEv(gid);
            }
        }
    }

    void VisibilitySystem::cellUnsubscribe(ObjectHandle who, int gid, int lcid, bool asPlayer, bool /*emitEvents*/)
    {
        auto* gp = spatialIndex_.tryGetGrid(gid);
        if (!gp)
            return;
        auto* cp = spatialIndex_.tryGetCell(*gp, lcid);
        if (!cp)
            return;

        auto& g = *const_cast<GridChunk*>(gp);
        auto& cb = *const_cast<CellChunk*>(cp);

        bool activatorsWasNonEmpty = false;
        bool removed = false;
        bool activatorsNowEmpty = false;
        std::size_t nowSize = 0;

        {
            std::unique_lock<std::shared_mutex> lk(cb.mtx);
            auto& list = asPlayer ? cb.viewers : cb.activators;

            if (!asPlayer)
                activatorsWasNonEmpty = !cb.activators.empty();

            const auto oldSize = list.size();
            list.erase(std::remove(list.begin(), list.end(), who), list.end());
            removed = list.size() != oldSize;
            nowSize = list.size();

            if (!asPlayer)
                activatorsNowEmpty = cb.activators.empty();
        }

        if (!asPlayer && removed && activatorsWasNonEmpty && activatorsNowEmpty)
        {
            sLogger.debug("vis: cellUnsubscribe role=activator gid={} lcid={} nowSize={}", gid, lcid, nowSize);
            const int after = g.activeCells.fetch_sub(1, std::memory_order_relaxed) - 1;
            if (after <= 0)
            {
                if (after < 0)
                {
                    sLogger.warning("vis: activeCells underflow on grid {}, resetting to 0", gid);
                    g.activeCells.store(0, std::memory_order_relaxed);
                }

                // Grid activity is controlled only by activators. Physical spawned
                // objects stay until the delayed grid unload callback removes them.
                g.idleSince = std::chrono::steady_clock::now();
                for (auto& cbEv : hub_.onGridDeactivated)
                    cbEv(gid);
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Publishing implementation
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::emitPublishedForViewer(ObjectHandle viewerH, bool isSubscribe)
    {
        const ObjectSlot* viewer = nullptr;
        if (!spatialIndex_.tryGet(viewerH, viewer))
            return;
        if (!viewer->sub.active || !viewer->sub.viewer)
            return;

        const bool viewerIsPlayer = (viewer->meta.container == Container::Players);

        auto processCandidate = [&](ObjectHandle objH)
        {
            const ObjectSlot* obj = nullptr;
            if (!spatialIndex_.tryGet(objH, obj))
                return;
            if (!isPublishActive(obj->pub))
                return;
            if (obj->pub.playersOnly && !viewerIsPlayer)
                return;

            const int objG = SpatialIndex::packGridFromPos(obj->pos);
            const int objL = SpatialIndex::packCellFromPos(obj->pos);
            const int dist = cellChebDistGlobal(viewer->sub.gid, viewer->sub.lcx, viewer->sub.lcy, objG, objL);
            if (dist <= viewer->sub.radius)
                return; // normal ring handles this pair

            if (isSubscribe)
                updateVisibilityPair(viewerH, objH);
            else
                emitHiddenOnce(viewer->meta.guid, obj->meta.guid);
        };

        const auto stamp = nextCandidateQueryStamp();

        auto processOnce = [&](ObjectHandle objH)
        {
            if (!markCandidateOnce(objH, stamp))
                return;
            processCandidate(objH);
        };

        // Player->Player publish is the high-cardinality case. Query it spatially
        // around the viewer instead of scanning a global published-player list.
        if (viewerIsPlayer)
        {
            std::vector<ObjectHandle> players;
            players.reserve(128);
            const int playerPublishCells = cfg_.playerPublishCells;
            collectPlayerPublishersAroundCell(viewer->sub.gid, viewer->sub.lcx, viewer->sub.lcy,
                playerPublishCells, players, stamp);
            for (auto objH : players)
                processCandidate(objH);
        }

        // Grid-wide and custom cell-radius publishers should be rare. Keeping them
        // in small buckets avoids scanning all owners in the surrounding grids.
        for (auto objH : gridWidePublishers_)
            processOnce(objH);

        for (auto objH : cellRadiusPublishers_)
            processOnce(objH);
    }

    void VisibilitySystem::emitPublishedForPublisherMove(ObjectHandle pubH, int oldGid, int oldLcid, int newGid, int newLcid)
    {
        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(pubH, obj))
            return;
        if (!isPublishActive(obj->pub))
            return;

        const bool playersOnly = obj->pub.playersOnly;

        const int C = Cell::CellsPerTile;
        const int publishCells = (obj->pub.mode == PublishMode::GridWide)
            ? cfg_.gridWidePublishCells
            : obj->pub.extraCells;
        const int gridsRadius = (publishCells + C - 1) / C;

        auto collectViewersAround = [&](int baseGid, std::uint64_t stamp, std::vector<ObjectHandle>& out)
            {
                auto [bgx, bgy] = unpackGridId(baseGid);
                for (int dgy = -gridsRadius; dgy <= gridsRadius; ++dgy)
                {
                    for (int dgx = -gridsRadius; dgx <= gridsRadius; ++dgx)
                    {
                        const int gx = std::clamp(bgx + dgx, 0, Terrain::TilesCount - 1);
                        const int gy = std::clamp(bgy + dgy, 0, Terrain::TilesCount - 1);
                        const int gid = packGridId(gx, gy);

                        const GridChunk* gp = spatialIndex_.tryGetGrid(gid);
                        if (!gp)
                            continue;

                        std::shared_lock lg(gp->mtx);
                        for (auto vh : gp->owners[static_cast<std::size_t>(Container::Players)])
                            if (markCandidateOnce(vh, stamp))
                                out.push_back(vh);
                    }
                }
            };

        const auto stamp = nextCandidateQueryStamp();
        std::vector<ObjectHandle> viewers;
        viewers.reserve(256);
        collectViewersAround(oldGid, stamp, viewers);
        collectViewersAround(newGid, stamp, viewers);

        for (auto vh : viewers)
        {
            const ObjectSlot* viewer = nullptr;
            if (!spatialIndex_.tryGet(vh, viewer))
                continue;
            if (!viewer->sub.active || !viewer->sub.viewer)
                continue;
            if (playersOnly && viewer->meta.container != Container::Players)
                continue;

            const auto inRing = [&](int d) { return d <= viewer->sub.radius; };
            const auto inPub = [&](int gid, int d)
            {
                if (inRing(d))
                    return false;

                if (obj->pub.mode == PublishMode::GridWide)
                    return d <= cfg_.gridWidePublishCells;

                return obj->pub.extraCells > 0 && d <= std::max(viewer->sub.radius, obj->pub.extraCells);
            };

            const int oldDist = cellChebDistGlobal(viewer->sub.gid, viewer->sub.lcx, viewer->sub.lcy, oldGid, oldLcid);
            const int newDist = cellChebDistGlobal(viewer->sub.gid, viewer->sub.lcx, viewer->sub.lcy, newGid, newLcid);

            const bool oldPub = inPub(oldGid, oldDist);
            const bool newPub = inPub(newGid, newDist);
            const bool newRing = inRing(newDist);
            const bool oldRing = inRing(oldDist);

            if (!oldPub && !oldRing && newPub)
            {
                emitVisibleOnce(viewer->meta.guid, obj->meta.guid);
                continue;
            }
            if (oldPub && !newPub && !newRing)
            {
                emitHiddenOnce(viewer->meta.guid, obj->meta.guid);
                continue;
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Notifications (spawn/despawn/move/publish)
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::notifySpawn(ObjectHandle h, int /*gid*/, int /*lcid*/, bool /*alsoActivators*/)
    {
        std::vector<ObjectHandle> viewers;
        collectCandidateViewersForObject(h, viewers);

        for (auto viewerH : viewers)
            updateVisibilityPair(viewerH, h);
    }

    void VisibilitySystem::notifyDespawn(ObjectHandle h, int /*gid*/, int /*lcid*/, bool /*alsoActivators*/)
    {
        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(h, obj))
            return;

        std::vector<ObjectHandle> viewers;
        collectCandidateViewersForObject(h, viewers);

        for (auto viewerH : viewers)
        {
            const ObjectSlot* viewer = nullptr;
            if (!spatialIndex_.tryGet(viewerH, viewer))
                continue;

            emitHiddenOnce(viewer->meta.guid, obj->meta.guid);
        }
    }

    void VisibilitySystem::notifyPublished(ObjectHandle objH, int /*objGid*/, int /*objLcid*/, bool isSpawn)
    {
        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(objH, obj))
            return;
        if (!isPublishActive(obj->pub))
            return;

        std::vector<ObjectHandle> viewers;
        collectCandidateViewersForObject(objH, viewers);

        for (auto viewerH : viewers)
        {
            if (isSpawn)
            {
                updateVisibilityPair(viewerH, objH);
            }
            else
            {
                const ObjectSlot* viewer = nullptr;
                if (!spatialIndex_.tryGet(viewerH, viewer))
                    continue;
                emitHiddenOnce(viewer->meta.guid, obj->meta.guid);
            }
        }
    }

    void VisibilitySystem::notifyCrossCellMove(ObjectHandle objH, int oldGid, int oldLcid, int newGid, int newLcid)
    {
        if (oldGid == newGid && oldLcid == newLcid)
            return;

        const ObjectSlot* obj = nullptr;
        if (!spatialIndex_.tryGet(objH, obj))
            return;

        auto* movedObj = static_cast<Object*>(obj->native);

        std::vector<ObjectHandle> oldSubs;
        if (const GridChunk* ogp = spatialIndex_.tryGetGrid(oldGid))
        {
            if (const CellChunk* ocp = spatialIndex_.tryGetCell(*ogp, oldLcid))
            {
                std::shared_lock lk(ocp->mtx);
                oldSubs = ocp->viewers;
            }
        }

        std::vector<ObjectHandle> newSubs;
        if (const GridChunk* ngp = spatialIndex_.tryGetGrid(newGid))
        {
            if (const CellChunk* ncp = spatialIndex_.tryGetCell(*ngp, newLcid))
            {
                std::shared_lock lk(ncp->mtx);
                newSubs = ncp->viewers;
            }
        }

        auto toViewerKeySet = [&](const std::vector<ObjectHandle>& v)
            {
                std::unordered_set<uint64_t> s; // key = (id<<32)|gen
                s.reserve(v.size());
                for (auto h : v) {
                    const ObjectSlot* viewer = nullptr;
                    if (!spatialIndex_.tryGet(h, viewer)) continue;
                    if (!viewer->sub.active || !viewer->sub.viewer) continue;
                    s.insert((uint64_t(h.id) << 32) | h.gen);
                }
                return s;
            };

        auto oldSet = toViewerKeySet(oldSubs);
        auto newSet = toViewerKeySet(newSubs);

        std::vector<uint64_t> lost;   lost.reserve(oldSet.size());
        std::vector<uint64_t> gained; gained.reserve(newSet.size());

        for (auto k : oldSet) if (!newSet.contains(k)) lost.push_back(k);
        for (auto k : newSet) if (!oldSet.contains(k)) gained.push_back(k);

        auto unkey = [](uint64_t k) { return ObjectHandle{ uint32_t(k >> 32), uint32_t(k) }; };

        for (auto k : lost)
        {
            auto vh = unkey(k);
            const ObjectSlot* viewer = nullptr;
            if (!spatialIndex_.tryGet(vh, viewer)) continue;

            updateVisibilityPair(vh, objH);
        }

        for (auto k : gained)
        {
            auto vh = unkey(k);
            const ObjectSlot* viewer = nullptr;
            if (!spatialIndex_.tryGet(vh, viewer) || !viewer->native) continue;
            if (!viewer->sub.active || !viewer->sub.viewer) continue;

            updateVisibilityPair(vh, objH);
        }
    }

    void VisibilitySystem::reconcilePublishedForViewerMove(ObjectHandle viewerH,
        int oldGid, int oldLcid, int newGid, int newLcid)
    {
        const ObjectSlot* viewer = nullptr;
        if (!spatialIndex_.tryGet(viewerH, viewer) || !viewer->sub.active || !viewer->sub.viewer)
            return;

        const bool viewerIsPlayer = (viewer->meta.container == Container::Players);
        const auto stamp = nextCandidateQueryStamp();

        auto processOnce = [&](ObjectHandle objH)
        {
            if (!markCandidateOnce(objH, stamp))
                return;
            reconcilePublishedCandidateForViewerMove(viewerH, *viewer, objH, oldGid, oldLcid, newGid, newLcid);
        };

        if (viewerIsPlayer)
        {
            auto [oldLcx, oldLcy] = unpackCellId2(oldLcid);
            auto [newLcx, newLcy] = unpackCellId2(newLcid);
            const int playerPublishCells = cfg_.playerPublishCells;

            std::vector<ObjectHandle> players;
            players.reserve(128);
            collectPlayerPublishersAroundCell(oldGid, oldLcx, oldLcy, playerPublishCells, players, stamp);
            collectPlayerPublishersAroundCell(newGid, newLcx, newLcy, playerPublishCells, players, stamp);

            for (auto objH : players)
                reconcilePublishedCandidateForViewerMove(viewerH, *viewer, objH, oldGid, oldLcid, newGid, newLcid);
        }

        for (auto objH : gridWidePublishers_)
            processOnce(objH);

        for (auto objH : cellRadiusPublishers_)
            processOnce(objH);
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Emit visible/hidden (deduped)
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::emitVisibleOnce(const WoWGuid& viewer, const WoWGuid& object)
    {
#ifndef VIS_TRACK_VISIBLE
        for (auto& f : hub_.onVisible)
            f(viewer, object);
#else
        bool shouldEmit = false;
        {
            const uint64_t vraw = viewer.getRawGuid();
            const uint64_t oraw = object.getRawGuid();
            std::unique_lock<std::shared_mutex> lk(visibleNowMtx_);
            auto& set = visibleNow_[vraw];
            if (set.insert(oraw).second)
            {
                seenBy_[oraw].insert(vraw);
                shouldEmit = true;
            }
        }

        if (shouldEmit)
        {
            for (auto& f : hub_.onVisible)
                f(viewer, object);
        }
#endif
    }

    void VisibilitySystem::emitHiddenOnce(const WoWGuid& viewer, const WoWGuid& object)
    {
#ifndef VIS_TRACK_VISIBLE
        for (auto& f : hub_.onHidden)
            f(viewer, object);
#else
        bool shouldEmit = false;
        {
            const uint64_t vraw = viewer.getRawGuid();
            const uint64_t oraw = object.getRawGuid();
            std::unique_lock<std::shared_mutex> lk(visibleNowMtx_);
            if (auto it = visibleNow_.find(vraw); it != visibleNow_.end())
                shouldEmit = it->second.erase(oraw) > 0;

            if (shouldEmit)
            {
                if (auto jt = seenBy_.find(oraw); jt != seenBy_.end())
                {
                    jt->second.erase(vraw);
                    if (jt->second.empty())
                        seenBy_.erase(jt);
                }
            }
        }

        if (shouldEmit)
        {
            for (auto& f : hub_.onHidden)
                f(viewer, object);
        }
#endif
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    /// GUID index
    ////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
    /// Proximity sweeps
    ////////////////////////////////////////////////////////////////////////////////////////
    void VisibilitySystem::proximitySweepViewer(ObjectHandle viewerH)
    {
        const ObjectSlot* vs = nullptr;
        if (!spatialIndex_.tryGet(viewerH, vs))
            return;
        if (!vs->sub.active || !vs->sub.viewer)
            return;

        const int baseGid = vs->sub.gid;
        const int lcx = vs->sub.lcx;
        const int lcy = vs->sub.lcy;
        const int R = vs->sub.radius;

        auto ring = spatialIndex_.buildRingCells(baseGid, lcx, lcy, R);

        auto* viewerUnit = static_cast<Unit*>(vs->native);
        if (!viewerUnit)
            return;

        const auto stamp = nextCandidateQueryStamp();

        for (auto [gid, lcid] : ring)
        {
            const GridChunk* gp = spatialIndex_.tryGetGrid(gid); if (!gp) continue;
            const CellChunk* cp = spatialIndex_.tryGetCell(*gp, lcid); if (!cp) continue;

            std::shared_lock lk(cp->mtx);
            for (std::size_t ci = 0; ci < static_cast<std::size_t>(Container::Count); ++ci)
            {
                for (auto oh : cp->byContainer[ci])
                {
                    if (!markCandidateOnce(oh, stamp))
                        continue;

                    const ObjectSlot* os = nullptr;
                    if (!spatialIndex_.tryGet(oh, os) || !os->native)
                        continue;

                    auto* obj = static_cast<Object*>(os->native);
                    const bool visibleNow = viewerUnit->canSee(obj);

                    if (visibleNow) emitVisibleOnce(vs->meta.guid, os->meta.guid);
                    else            emitHiddenOnce(vs->meta.guid, os->meta.guid);
                }
            }
        }
    }

    void VisibilitySystem::proximityAffectViewersForObject(ObjectHandle objH)
    {
        const ObjectSlot* os = nullptr;
        if (!spatialIndex_.tryGet(objH, os) || !os->native)
            return;

        const int gid = SpatialIndex::packGridFromPos(os->pos);
        const int lcid = SpatialIndex::packCellFromPos(os->pos);

        const GridChunk* gp = spatialIndex_.tryGetGrid(gid);
        if (!gp)
            return;
        const CellChunk* cp = spatialIndex_.tryGetCell(*gp, lcid);
        if (!cp)
            return;

        std::vector<ObjectHandle> subs;
        {
            std::shared_lock lk(cp->mtx);
            subs = cp->viewers;
        }

        auto* obj = static_cast<Object*>(os->native);

        for (auto vh : subs)
        {
            const ObjectSlot* vs = nullptr;
            if (!spatialIndex_.tryGet(vh, vs) || !vs->native)
                continue;
            if (!vs->sub.active || !vs->sub.viewer)
                continue;

            auto* viewerUnit = static_cast<Unit*>(vs->native);
            const bool visibleNow = viewerUnit->canSee(obj);

            if (visibleNow) emitVisibleOnce(vs->meta.guid, os->meta.guid);
            else            emitHiddenOnce(vs->meta.guid, os->meta.guid);
        }
    }


} // namespace visibility
