/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ObjectRegistry.h"
#include "FactoryHolder.h"

class Creature;
class CreatureAI;
/*
template <class O, class AI>
struct SelectableAI : public FactoryHolder<AI, O>, public Permissible<O>
{
    SelectableAI(std::string const& name) : FactoryHolder<AI, O>(name), Permissible<O>() {}
};

template <class REAL_AI>
struct CreatureAIFactory : public SelectableAI<Creature, CreatureAI>
{
    CreatureAIFactory(std::string const& name) : SelectableAI<Creature, CreatureAI>(name) {}

    inline CreatureAI* create(Creature* c) const override
    {
        return new REAL_AI(c);
    }

    int32_t permit(Creature const* c) const override
    {
        return REAL_AI::permissible(c);
    }
};

typedef SelectableAI<Creature, CreatureAI>::FactoryHolderRegistry CreatureAIRegistry;*/

template <class REAL_AI>
class CreatureAIFactory : public FactoryHolder<CreatureAI, Creature>, public Permissible<Creature>
{
public:
    explicit CreatureAIFactory(const std::string& name) : FactoryHolder<CreatureAI, Creature>(name), Permissible<Creature>() {}

    // Erstellen einer neuen Instanz der AI
    inline CreatureAI* create(Creature* creature) const override
    {
        return new REAL_AI(creature);
    }

    // Bewertet, ob die AI für die Kreatur geeignet ist
    int32_t permit(Creature const* creature) const override
    {
        return REAL_AI::permissible(creature);
    }
};

using CreatureAIRegistry = FactoryHolder<CreatureAI, Creature>::FactoryHolderRegistry;
#define sCreatureAIRegistry CreatureAIRegistry::getInstance()