/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <unordered_map>
#include <cstdint>
#include <shared_mutex>
#include "WoWGuid.h"

class Object;
class Creature;
class GameObject;
class DynamicObject;
class Player;
class Pet;
class AreaTrigger;
class Corpse;
class Transporter;

struct Summary
{
    size_t any{ 0 }, creatures{ 0 }, gameobjects{ 0 }, dynamics{ 0 }, players{ 0 }, pets{ 0 }, corpses{ 0 }, total{ 0 };
};

namespace world
{
    class WorldObjectRegistry 
    {
    public:
        WorldObjectRegistry() = default;

        // Snapshots
        void snapshotCreatures(std::vector<Creature*>& out) const;
        void snapshotGameObjects(std::vector<GameObject*>& out) const;
        void snapshotTransporters(std::vector<Transporter*>& out) const;
        void snapshotDynamicObjects(std::vector<DynamicObject*>& out) const;
        void snapshotPlayers(std::vector<Player*>& out) const;
        void snapshotPets(std::vector<Pet*>& out) const;
        void snapshotCorpses(std::vector<Corpse*>& out) const;

        template <typename T, typename Fn>
        void forEachPinned(std::vector<T*>& scratch, Fn&& fn) const;

        template <class Fn>
        void forEachPinnedByGuids(const std::vector<WoWGuid>& guids, Fn&& fn);

        template <class T, class Fn>
        void forEachPinnedByGuidsT(const std::vector<WoWGuid>& guids, Fn&& fn);

        // Insert/remove (Map-thread recommended). Thread-safe via shared_mutex.
        void insert(Object* obj);
        void erase(Object* obj);
        void erase(const WoWGuid& g);
        bool contains(const WoWGuid& g) const;

        // Typed getters (GUID → pointer). Return nullptr if absent.
        Creature*       getCreature     (const WoWGuid& g) const;
        GameObject*     getGameObject   (const WoWGuid& g) const;
        Transporter*    getTransporter  (const WoWGuid& g) const;
        DynamicObject*  getDynamicObject(const WoWGuid& g) const;
        Player*         getPlayer       (const WoWGuid& g) const;
        Player*         getPlayer       (const uint32_t low) const;
        Pet*            getPet          (const WoWGuid& g) const;
        Corpse*         getCorpse       (const WoWGuid& g) const;
        Object*         getAny          (const WoWGuid& g) const; // returns base if any

        // Utilities
        void   clearAll();

        size_t countAny()        const; // = any_.size()
        size_t countCreatures()  const;
        size_t countGameObjects()const;
        size_t countDynamics()   const;
        size_t countPlayers()    const;
        size_t countPets()       const;
        size_t countCorpses()    const;
        size_t countTransporters() const;

        size_t countAll()        const;
        Summary counts() const;


    private:
        using key_t = std::uint64_t;
        static inline key_t keyOf(const WoWGuid& g) { return g.getRawGuid(); }

        mutable std::shared_mutex mtx_;
        std::unordered_map<key_t, Object*>        any_;
        std::unordered_map<key_t, Creature*>      creatures_;
        std::unordered_map<key_t, GameObject*>    gameobjects_;
        std::unordered_map<key_t, Transporter*>   transporters_;
        std::unordered_map<key_t, DynamicObject*> dynamics_;
        std::unordered_map<key_t, Player*>        players_;
        std::unordered_map<key_t, Pet*>           pets_;
        std::unordered_map<key_t, Corpse*>        corpses_;
    };

    template<typename T, typename Fn>
    inline void WorldObjectRegistry::forEachPinned(std::vector<T*>& scratch, Fn&& fn) const
    {
        for (T* obj : scratch)
        {
            if (!obj)
                continue;
            typename T::UpdatePin _pin(obj);   // RAII-Pin
            fn(*obj);
            // _pin -> dtor: unpin
        }
    }
    template<class Fn>
    inline void WorldObjectRegistry::forEachPinnedByGuids(const std::vector<WoWGuid>& guids, Fn&& fn)
    {
        for (const WoWGuid& g : guids)
        {
            Object* obj = nullptr;

            {
                std::shared_lock lk(mtx_);
                auto it = any_.find(keyOf(g));
                if (it == any_.end() || !it->second)
                    continue;

                obj = it->second;

                Object::UpdatePin _pin(obj); // RAII-Pin
                lk.unlock();
                fn(*obj);

                // _pin -> dtor: unpin
            }
        }

    }

    template<class T, class Fn>
    inline void WorldObjectRegistry::forEachPinnedByGuidsT(const std::vector<WoWGuid>& guids, Fn&& fn)
    {
        for (const WoWGuid& g : guids)
        {
            T* obj = nullptr;

            {
                std::shared_lock lk(mtx_);

                auto it = any_.find(keyOf(g));
                if (it == any_.end() || !it->second)
                    continue;

                obj = dynamic_cast<T*>(it->second);
                if (!obj)
                    continue;

                typename T::UpdatePin _pin(obj); // RAII-Pin
                lk.unlock();
                fn(*obj);

                // _pin -> dtor: unpin
            }
        }
    }
}
