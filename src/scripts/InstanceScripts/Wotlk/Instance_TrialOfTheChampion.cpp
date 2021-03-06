/*
Copyright (c) 2014-2020 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "Setup.h"
#include "Instance_TrialOfTheChampion.h"
#include <Units/Creatures/Pet.h>

class TrialOfTheChampionInstanceScript : public InstanceScript
{
public:

    explicit TrialOfTheChampionInstanceScript(MapMgr* pMapMgr) : InstanceScript(pMapMgr)
    {
    }

    static InstanceScript* Create(MapMgr* pMapMgr) { return new TrialOfTheChampionInstanceScript(pMapMgr); }
};

void SetupTrialOfTheChampion(ScriptMgr* mgr)
{
    mgr->register_instance_script(MAP_TRIAL_OF_THE_CHAMPION, &TrialOfTheChampionInstanceScript::Create);
}
