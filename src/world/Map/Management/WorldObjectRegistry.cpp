/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "WorldObjectRegistry.hpp"
#include <type_traits>
#include "Objects/Object.hpp"
#include "Objects/Units/Creatures/Creature.h"
#include "Objects/GameObject.h"
#include "Objects/DynamicObject.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Objects/Units/Creatures/Pet.h"
#include "Objects/Units/Creatures/Corpse.hpp"
#include "Objects/Transporter.hpp"


namespace world 
{
    void WorldObjectRegistry::snapshotCreatures(std::vector<Creature*>& out) const
    {
        std::shared_lock lk(mtx_);
        out.clear(); out.reserve(creatures_.size());
        for (auto& kv : creatures_) out.push_back(kv.second);
    }

    void WorldObjectRegistry::snapshotGameObjects(std::vector<GameObject*>& out) const
    {
        std::shared_lock lk(mtx_);
        out.clear(); out.reserve(gameobjects_.size());
        for (auto& kv : gameobjects_) out.push_back(kv.second);
    }

    void WorldObjectRegistry::snapshotTransporters(std::vector<Transporter*>& out) const
    {
        std::shared_lock lk(mtx_);
        out.clear(); out.reserve(transporters_.size());
        for (auto& kv : transporters_) out.push_back(kv.second);
    }

    void WorldObjectRegistry::snapshotDynamicObjects(std::vector<DynamicObject*>& out) const
    {
        std::shared_lock lk(mtx_);
        out.clear(); out.reserve(dynamics_.size());
        for (auto& kv : dynamics_) out.push_back(kv.second);
    }

    void WorldObjectRegistry::snapshotPlayers(std::vector<Player*>& out) const
    {
        std::shared_lock lk(mtx_);
        out.clear(); out.reserve(players_.size());
        for (auto& kv : players_) out.push_back(kv.second);
    }

    void WorldObjectRegistry::snapshotPets(std::vector<Pet*>& out) const
    {
        std::shared_lock lk(mtx_);
        out.clear(); out.reserve(pets_.size());
        for (auto& kv : pets_) out.push_back(kv.second);
    }

    void WorldObjectRegistry::snapshotCorpses(std::vector<Corpse*>& out) const
    {
        std::shared_lock lk(mtx_);
        out.clear(); out.reserve(corpses_.size());
        for (auto& kv : corpses_) out.push_back(kv.second);
    }

    void WorldObjectRegistry::insert(Object* obj)
    {
        if (!obj) return;
        const key_t k = keyOf(obj->GetNewGUID());
        std::unique_lock lk(mtx_);

        if (auto it = any_.find(k); it != any_.end() && it->second != obj)
        {
            sLogger.warning("Registry: GUID {} already mapped to {}, overwriting with {}",
                             k, (void*)it->second, (void*)obj);
        }

        any_[k] = obj;

        if (auto* p = dynamic_cast<Player*>(obj)) players_[k] = p;
        else if (auto* c = dynamic_cast<Creature*>(obj)) creatures_[k] = c;
        else if (auto* tr = dynamic_cast<Transporter*>(obj)) transporters_[k] = tr;
        else if (auto* go = dynamic_cast<GameObject*>(obj)) gameobjects_[k] = go;
        else if (auto* d = dynamic_cast<DynamicObject*>(obj)) dynamics_[k] = d;
        else if (auto* pet = dynamic_cast<Pet*>(obj)) pets_[k] = pet;
        else if (auto* co = dynamic_cast<Corpse*>(obj)) corpses_[k] = co;
    }

    void WorldObjectRegistry::erase(Object* obj)
    {
        if (!obj)
            return;

        erase(obj->GetNewGUID());
    }

    void WorldObjectRegistry::erase(const WoWGuid& g)
    {
        const key_t k = keyOf(g);
        std::unique_lock lk(mtx_);
        any_.erase(k);
        players_.erase(k);
        creatures_.erase(k);
        gameobjects_.erase(k);
        dynamics_.erase(k);
        pets_.erase(k);
        corpses_.erase(k);
        transporters_.erase(k);
    }

    bool WorldObjectRegistry::contains(const WoWGuid& g) const
    {
        std::shared_lock lk(mtx_);
        return any_.find(keyOf(g)) != any_.end();
    }

    // ---- Getters ----
    Creature* WorldObjectRegistry::getCreature(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = creatures_.find(keyOf(g));   return it == creatures_.end() ? nullptr : it->second; }
    GameObject* WorldObjectRegistry::getGameObject(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = gameobjects_.find(keyOf(g)); return it == gameobjects_.end() ? nullptr : it->second; }
    Transporter* WorldObjectRegistry::getTransporter(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = transporters_.find(keyOf(g)); return it == transporters_.end() ? nullptr : it->second; }
    DynamicObject* WorldObjectRegistry::getDynamicObject(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = dynamics_.find(keyOf(g));    return it == dynamics_.end() ? nullptr : it->second; }
    Player* WorldObjectRegistry::getPlayer(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = players_.find(keyOf(g));     return it == players_.end() ? nullptr : it->second; }
    Player* WorldObjectRegistry::getPlayer(uint32_t low) const
    {
        std::shared_lock lk(mtx_);
        auto it = players_.find(static_cast<key_t>(low));
        return it == players_.end() ? nullptr : it->second;
    }

    Pet* WorldObjectRegistry::getPet(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = pets_.find(keyOf(g));        return it == pets_.end() ? nullptr : it->second; }
    Corpse* WorldObjectRegistry::getCorpse(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = corpses_.find(keyOf(g));     return it == corpses_.end() ? nullptr : it->second; }
    Object* WorldObjectRegistry::getAny(const WoWGuid& g) const { std::shared_lock lk(mtx_); auto it = any_.find(keyOf(g));         return it == any_.end() ? nullptr : it->second; }

    void WorldObjectRegistry::clearAll()
    {
        std::unique_lock lk(mtx_);
        any_.clear();
        players_.clear();
        creatures_.clear();
        gameobjects_.clear();
        dynamics_.clear();
        pets_.clear();
        corpses_.clear();
        transporters_.clear();
    }

    size_t WorldObjectRegistry::countAny()         const { std::shared_lock lk(mtx_); return any_.size(); }
    size_t WorldObjectRegistry::countCreatures()   const { std::shared_lock lk(mtx_); return creatures_.size(); }
    size_t WorldObjectRegistry::countGameObjects() const { std::shared_lock lk(mtx_); return gameobjects_.size(); }
    size_t WorldObjectRegistry::countDynamics()    const { std::shared_lock lk(mtx_); return dynamics_.size(); }
    size_t WorldObjectRegistry::countPlayers()     const { std::shared_lock lk(mtx_); return players_.size(); }
    size_t WorldObjectRegistry::countPets()        const { std::shared_lock lk(mtx_); return pets_.size(); }
    size_t WorldObjectRegistry::countCorpses()     const { std::shared_lock lk(mtx_); return corpses_.size(); }
    size_t WorldObjectRegistry::countTransporters() const{ std::shared_lock lk(mtx_); return transporters_.size(); }

    size_t WorldObjectRegistry::countAll() const
    {
        std::shared_lock lk(mtx_);
        return creatures_.size() + gameobjects_.size() + dynamics_.size()  + players_.size() + pets_.size() + corpses_.size() + transporters_.size();
    }

    Summary WorldObjectRegistry::counts() const
    {
        std::shared_lock lk(mtx_);
        Summary s;
        s.any = any_.size();
        s.creatures = creatures_.size();
        s.gameobjects = gameobjects_.size();
        s.dynamics = dynamics_.size();
        s.players = players_.size();
        s.pets = pets_.size();
        s.corpses = corpses_.size();
        s.total = s.creatures + s.gameobjects + s.dynamics + s.players + s.pets + s.corpses;
        return s;
    }

} // namespace world