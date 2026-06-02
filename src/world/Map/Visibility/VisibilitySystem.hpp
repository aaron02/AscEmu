/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <list>
#include <limits>
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>
#include <chrono>
#include <cstdint>
#include "VisibilityTypes.hpp"
#include "CommandQueue.hpp"
#include "SpatialIndex.hpp"

#include "Objects/Units/Creatures/Creature.h"
#include "Objects/GameObject.h"
#include "Objects/Object.hpp"

#define VIS_TRACK_VISIBLE 1

class Unit;

//////////////////////////////////////////////////////////////////////////////////////////
/// Visibility namespace
//////////////////////////////////////////////////////////////////////////////////////////
namespace visibility
{
    ////////////////////////////////////////////////////////////////////////////////////////
    /// Aggregated visibility metrics snapshot. Intended for diagnostics and profiling.
    ////////////////////////////////////////////////////////////////////////////////////////
    struct VisSnapshot
    {
        size_t grids = 0;                                        ///< number of allocated grid chunks
        size_t activeGrids = 0;                                  ///< grids with activeCells > 0
        size_t cells = 0;                                        ///< allocated cell chunks
        size_t activeCells = 0;                                  ///< active cell references from activators
        size_t poolLive = 0;                                     ///< number of live objects in the pool
        size_t guidIndex = 0;                                    ///< number of GUIDs indexed
        size_t cellViewers = 0;                                  ///< total handles in CellChunk::viewers
        size_t cellActivators = 0;                               ///< total handles in CellChunk::activators
        size_t visibleViewers = 0;                               ///< visibleNow_ viewer entries
        size_t visiblePairs = 0;                                 ///< total visible pairs
        size_t seenObjects = 0;                                  ///< seenBy_ object entries
        size_t seenPairs = 0;                                    ///< total reverse visible pairs
        size_t gridWidePublishers = 0;                           ///< grid-wide publisher bucket size
        size_t cellRadiusPublishers = 0;                         ///< custom cell-radius publisher bucket size
        size_t nearCacheGuidsCapacity = 0;                       ///< retained NearCache guid capacity on live slots
        size_t nearCacheStampsCapacity = 0;                      ///< retained NearCache stamp capacity on live slots
        size_t nearCacheCachedGuids = 0;                         ///< currently cached near guid count
        std::array<size_t, static_cast<size_t>(visibility::Container::Count)> owners{}; ///< per-owner counters
    };

    ////////////////////////////////////////////////////////////////////////////////////////
    /// Visibility manager for a single map instance. Handles spatial indexing, subscriptions
    /// (viewers/activators), publishing, and event emission. Thread ownership: map thread unless
    /// otherwise stated (see Drain/FlushPendingMoves).
    ////////////////////////////////////////////////////////////////////////////////////////
    class VisibilitySystem
    {
    public:

        explicit VisibilitySystem(SpatialIndex& spatialIndex, Config cfg = {}, std::size_t queueCap = 65536);
        ~VisibilitySystem() = default;

        VisibilitySystem(const VisibilitySystem&) = delete;
        VisibilitySystem& operator=(const VisibilitySystem&) = delete;
        VisibilitySystem(VisibilitySystem&&) = delete;
        VisibilitySystem& operator=(VisibilitySystem&&) = delete;

        ////////////////////////////////////////////////////////////////////////////////////
        /// Metrics
        ////////////////////////////////////////////////////////////////////////////////////
        /// Returns a diagnostic snapshot of relevant metrics.
        VisSnapshot snapshot() const;
        /// Logs the diagnostic snapshot with a reason label.
        void logMemoryDiagnostics(const char* reason) const;

        ////////////////////////////////////////////////////////////////////////////////////
        /// Object spatial lifecycle events
        ////////////////////////////////////////////////////////////////////////////////////
        /// Called after SpatialIndex::addObject() inserted an object into the spatial index.
        void onObjectAdded(ObjectHandle h);
        /// Called before SpatialIndex::removeObject() removes an object from the spatial index.
        void onObjectRemoving(ObjectHandle h);
        /// Called after SpatialIndex::moveObject() updated position/cell membership.
        void onObjectMoved(ObjectHandle h, const SpatialMoveResult& move);

        /// Applies queued visibility commands on the map thread. Process up to \a max items.
        void drain(std::size_t max = std::numeric_limits<std::size_t>::max());

        ////////////////////////////////////////////////////////////////////////////////////
        /// Visibility subscriptions
        ////////////////////////////////////////////////////////////////////////////////////
        /// Subscribes a viewer to a ring of cells around its current position.
        void subscribeViewer(ObjectHandle who, int radiusCells);
        /// Subscribes an activator (non-player publisher) to a ring of cells.
        void subscribeActivator(ObjectHandle who, int radiusCells);
        /// Removes any subscription of this handle.
        void unsubscribe(ObjectHandle who);

        // Runtime role changes. These are used by possess/unpossess, vehicles,
        // scripts, and any later system that turns an already-spawned object into
        // a viewer or activator without reattaching it to the map.
        void setViewerRole(ObjectHandle who, bool enabled, int radiusCells);
        void setActivatorRole(ObjectHandle who, bool enabled, int radiusCells);

        // temporary Visits a Grid
        void activateGrid(int gid);

        ////////////////////////////////////////////////////////////////////////////////////
        /// Interest profiles
        ////////////////////////////////////////////////////////////////////////////////////
        /// Builds the default interest profile for an object type.
        InterestProfile buildInterestProfile(Object* obj) const;
        /// Applies viewer/activator subscriptions and publishing for an object.
        void applyInterestProfile(ObjectHandle who, const InterestProfile& profile);

        ////////////////////////////////////////////////////////////////////////////////////
        /// Visibility publishing
        ////////////////////////////////////////////////////////////////////////////////////
        /// Publishes an object to viewers within (subscription radius + extraCells).
        void publish(ObjectHandle who, int extraCells, bool playersOnly = true);
        /// Stops publishing the given object.
        void unpublish(ObjectHandle who);

        ////////////////////////////////////////////////////////////////////////////////////
        /// Tick
        ////////////////////////////////////////////////////////////////////////////////////
        /// Advances internal time and performs budgeted work.
        void tick(std::chrono::milliseconds dt);

        ////////////////////////////////////////////////////////////////////////////////////
        /// Events (callbacks will be executed on the map thread)
        ////////////////////////////////////////////////////////////////////////////////////
        void onBecameVisible(VisibleCb cb) { hub_.onVisible.push_back(std::move(cb)); }
        void onBecameHidden(HiddenCb cb) { hub_.onHidden.push_back(std::move(cb)); }
        void onGridChanged(GridMoveCb cb) { hub_.onGridChanged.push_back(std::move(cb)); }
        void onGridActivated(GridEventCb cb) { hub_.onGridActivated.push_back(std::move(cb)); }
        void onGridDeactivated(GridEventCb cb) { hub_.onGridDeactivated.push_back(std::move(cb)); }
        void onGridUnload(GridEventCb cb) { hub_.onGridUnload.push_back(std::move(cb)); }

        ////////////////////////////////////////////////////////////////////////////////////
        /// Index helpers
        ////////////////////////////////////////////////////////////////////////////////////
        /// Rebuilds viewer proximity based on current position.
        void proximitySweepViewer(ObjectHandle viewerH);
        /// Reconciles viewers affected by a specific object's movement.
        void proximityAffectViewersForObject(ObjectHandle objH);
        /// Re-evaluates visibility for an object after non-positional visibility state changes
        /// such as stealth, invisibility, death state, GM invis, faction/phase changes, etc.
        void refreshObjectVisibility(ObjectHandle objH);

        // Collect all viewers currently seeing an object.
        void collectViewersOf(const WoWGuid& object, std::vector<WoWGuid>& out) const;
        // Collect all objects currently visible for a viewer. Used when a temporary
        // viewer source such as possession/farsight is removed and the recipient
        // player's client-side visible cache must be cleaned explicitly.
        void collectVisibleObjectsForViewer(const WoWGuid& viewer, std::vector<WoWGuid>& out) const;
        // Removes all tracked visible objects for a viewer and returns the removed
        // object GUIDs. Used for hard client visibility resets on same-map
        // teleports/repop such as Release Spirit.
        void clearVisibleObjectsForViewer(const WoWGuid& viewer, std::vector<WoWGuid>& out);

        // Hard-resets a viewer before same-map relocation. This removes the viewer
        // and activator cell subscriptions immediately, clears tracked visibility
        // pairs, and returns all previously visible object GUIDs so the caller can
        // send one explicit OutOfRange batch to the client.
        void resetViewerForRelocation(ObjectHandle who, std::vector<WoWGuid>& out);

    private:

        Config cfg_{};
        SpatialIndex& spatialIndex_;
        EventHub hub_{};
        CommandQueue cmdQueue_;

        // Published-object buckets. Player->Player CellRadius is intentionally not
        // stored here; it is resolved through SpatialIndex around the viewer/object
        // so it scales with local player density instead of total map population.
        std::vector<ObjectHandle> gridWidePublishers_;
        std::vector<ObjectHandle> cellRadiusPublishers_;

        // Monotonic scratch stamp for candidate dedupe. This avoids allocating
        // unordered_set instances in hot visibility paths. Map-thread only.
        mutable std::uint64_t candidateQueryStamp_{ 0 };

        // Full grid-unload scan is O(active grid chunks). Do not do it every map
        // update; grid unload has minute-scale latency anyway.
        std::chrono::steady_clock::time_point lastGridUnloadScan_{};

        static inline uint64_t makeKey(ObjectHandle h) noexcept { return (static_cast<uint64_t>(h.id) << 32) | static_cast<uint64_t>(h.gen); }

        // Commands → apply (worker-only)
        void apply(const WorldCmd& cmd);

        // Subscriptions
        void subscribeRing(ObjectHandle who, int gid, int lcx, int lcy, int r, bool asPlayer);
        void unsubscribeRing(ObjectHandle who, int baseGid, int lcx, int lcy, int r, bool asPlayer);
        void cellSubscribe(ObjectHandle who, int gid, int lcid, bool asPlayer, bool emitEvents);
        void cellUnsubscribe(ObjectHandle who, int gid, int lcid, bool asPlayer, bool emitEvents);

        // Publish
        void emitPublishedForViewer(ObjectHandle viewerH, bool isSubscribe);
        void emitPublishedForPublisherMove(ObjectHandle pubH, int oldGid, int oldLcid, int newGid, int newLcid);

        bool isPlayerSpatialPublisher(const ObjectSlot& obj) const;
        bool isBucketedPublisher(const ObjectSlot& obj) const;
        void addPublishBucket(ObjectHandle who, const ObjectSlot& obj);
        void removePublishBucket(ObjectHandle who);
        bool isInPublishBucket(ObjectHandle who) const;
        bool hasCellSubscription(ObjectHandle who) const;
        std::uint64_t nextCandidateQueryStamp() const;
        bool markCandidateOnce(ObjectHandle h, std::uint64_t stamp) const;
        void collectPlayerPublishersAroundCell(int gid, int lcx, int lcy, int radiusCells,
            std::vector<ObjectHandle>& out, std::uint64_t stamp) const;
        void reconcilePublishedCandidateForViewerMove(ObjectHandle viewerH, const ObjectSlot& viewer, ObjectHandle objH,
            int oldGid, int oldLcid, int newGid, int newLcid);

        // Notifications
        void notifySpawn(ObjectHandle h, int gid, int lcid, bool alsoActivators);
        void notifyDespawn(ObjectHandle h, int gid, int lcid, bool alsoActivators);
        void notifyPublished(ObjectHandle objH, int objGid, int objLcid, bool isSpawn);
        void notifyCrossCellMove(ObjectHandle objH, int oldGid, int oldLcid, int newGid, int newLcid);

        void reconcilePublishedForViewerMove(ObjectHandle viewerH, int oldGid, int oldLcid, int newGid, int newLcid);

        // Central visibility rule / candidate helpers
        bool isWithinInterestReach(const ObjectSlot& viewer, const ObjectSlot& obj, int objGid, int objLcid) const;
        bool shouldViewerSeeObject(ObjectHandle viewerH, ObjectHandle objH) const;
        void updateVisibilityPair(ObjectHandle viewerH, ObjectHandle objH);
        void collectCandidateViewersForObject(ObjectHandle objH, std::vector<ObjectHandle>& out) const;

        // Object update (emit once semantics)
        void emitVisibleOnce(const WoWGuid& viewer, const WoWGuid& object);
        void emitHiddenOnce(const WoWGuid& viewer, const WoWGuid& object);


        bool isPublishedVisibleFor(const ObjectSlot* obj, const ObjectSlot* viewer, int objGid, int objLcid) const;
        void hideObjectsInCellsForViewer(ObjectHandle viewerH, const std::vector<CellRef>& cells);
        void hideObjectFromKnownViewers(const WoWGuid& objectGuid);
        bool isVisibleToViewer(const ObjectSlot* obj, const ObjectSlot* viewer, int objGid, int objLcid) const;

        bool shouldRefreshViewerInterest(const ObjectSlot& viewer, const SpatialMoveResult& move) const;
        void markViewerInterestRefreshed(ObjectSlot& viewer, const LocationVector& pos);
        void refreshViewerInterest(ObjectHandle viewerH, ObjectSlot& viewer);

        bool shouldRefreshMovedObjectInterest(const ObjectSlot& object, const SpatialMoveResult& move) const;
        void markMovedObjectInterestRefreshed(ObjectSlot& object, const LocationVector& pos);
        void refreshMovedObjectInterest(ObjectHandle objectH, ObjectSlot& object);

#ifdef VIS_TRACK_VISIBLE
        // "visibleNow" tracking (viewer GUID -> set of object GUIDs)
        std::unordered_map<uint64_t, std::unordered_set<uint64_t>> visibleNow_;
        // reverse index (object GUID -> set of viewer GUIDs)
        std::unordered_map<uint64_t, std::unordered_set<uint64_t>> seenBy_;
        mutable std::shared_mutex visibleNowMtx_;
#endif

        /// Small helper: check vector membership.
        static inline bool vecContains(const std::vector<ObjectHandle>& v, ObjectHandle h)
        {
            return std::find(v.begin(), v.end(), h) != v.end();
        }
    };

} // namespace visibility
