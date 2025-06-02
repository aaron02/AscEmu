/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "CreatureAIManager.h"

template <class T, class Value>
inline int32_t GetPermitFor(T const* obj, Value const& value)
{
    Permissible<T> const* const p = ASSERT_NOTNULL(dynamic_cast<Permissible<T> const*>(value.second.get()));
    return p->permit(obj);
}

template <class T>
struct PermissibleOrderPred
{
public:
    PermissibleOrderPred(T const* obj) : _obj(obj) {}

    template <class Value>
    bool operator()(Value const& left, Value const& right) const
    {
        return GetPermitFor(_obj, left) < GetPermitFor(_obj, right);
    }

private:
    T const* const _obj;
};

template <typename AIType>
inline FactoryHolder<AIType, Creature> const* SelectFactory(Creature* creature)
{
    using AIRegistry = typename FactoryHolder<AIType, Creature>::FactoryHolderRegistry;

    // select by permit check
    typename AIRegistry::RegistryMapType const& items = sCreatureAIRegistry->getRegisteredItems();
    auto itr = std::max_element(items.begin(), items.end(), PermissibleOrderPred<Creature>(creature));
    if (itr != items.end() && GetPermitFor(creature, *itr) >= 0)
        return itr->second.get();

    abort();
    return nullptr;
}

AI_Manager* AI_Manager::getInstance()
{
    static AI_Manager mInstance;
    return &mInstance;
}

CreatureAI* AI_Manager::selectAI(Creature* creature)
{
    if (creature->isPet())
        return ASSERT_NOTNULL(sCreatureAIRegistry->getRegistryItem("PetAI"))->create(creature);

    return SelectFactory<CreatureAI>(creature)->create(creature);
}
