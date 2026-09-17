Modern client script policy (Midnight / Forever)
==================================================

The legacy Classic-MoP script stack is intentionally disabled for modern
client profiles. This includes external script DLLs such as Battlegrounds,
SpellHandlers, EventScripts, GossipScripts, InstanceScripts, LuaEngine,
MiscScripts and QuestScripts.

Runtime loading is also blocked so stale DLLs left in the release script
directory cannot be loaded accidentally. Legacy spell-script setup, instance
script attachment and script-specific DB tables are skipped as well.

The core quest system, generic spell engine, maps, battleground core classes
and game-event system remain compiled; only their legacy script extensions are
disabled. Dedicated Midnight/Forever implementations should be added later
without re-enabling Classic-MoP modules globally.
