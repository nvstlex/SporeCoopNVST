# NVST COOP: four players across all five Spore stages

Status: target architecture and first portable contract; **not complete gameplay**.

The session supports one host and up to three guests. Each stage has its own native layout. Players cooperate against NPCs; separate player-owned objects do not imply war between players.

## Stage layouts

| Stage | Required layout | Ownership |
|---|---|---|
| Cell | One shared tide pool; one active cell per connected player | Each player controls their own cell |
| Creature | One shared nest | The nest contains the participating player species; each player controls their creature |
| Tribe | Up to four independent native tribes, spawned near the host tribe | Separate hut, citizens, food, tools and progression |
| Civilization | Up to four independent native civilizations; one initial city per player | Separate political ID, treasury, city list, vehicles and progression |
| Space | One shared starting solar system with up to four equivalent home planets | One home planet and empire per player; players can visit each other |

## Shared requirements

- Hard limit of four players; allocate only occupied slots in 1–3 player sessions.
- Host-authoritative simulation and validated guest commands.
- Network player IDs are not native faction IDs. An owner overlay does not create an independent faction.
- Replicate player-owned objects and reconstruct them on late join.
- Stage transitions, reload, reconnect and save/load must preserve ownership without duplicating objects.
- NPC factions remain opponents; players remain teammates.
- Single-player behavior remains unchanged outside a co-op session.

## Current implementation reality

### Cell

`cellsync.cpp` already synchronizes the shared pool, remote cells, food and damage. The local player controls its avatar while remote cells are mirrored. Guests now use deterministic player slots around the host: player 0 is the anchor, players 1–3 occupy a three-point ring, and the same slot is reused after avatar rebuild/load and after a knockout respawn. A guest is also placed when it first joins an already-open Cell session or re-enters Cell after another stage; this does not depend on the avatar index changing. The host position is the only guest anchor; if it is unavailable, a guest waits instead of drifting around another guest. The pure spawn-offset helper is covered by `tests/cell_tests.cpp`.

Still verify 2–4 players in the actual game, including the first hatch, KO/revive, death respawn, growth, level-up and host-away transitions. On a guest, `coop.pool` now reports `cell_guest_fx: slot=...`, `join_active`, `join_pending`, `join_tries` and the last target position; expected session IDs are host 0 and guests 1–3. The slot change does not yet prove every client sees the other three avatars correctly in all reload paths.

### Creature

The creature/social/NPC modules synchronize player creatures and the shared NPC world. nest.cpp currently mirrors NPC nest eggs; that alone does not prove the one shared player nest is complete. Verify that all player species and eggs appear in one nest and survive mating/evolution, stage transition and late join.

### Tribe

Current tribe.cpp assigns network owner IDs to members of one native player cTribe; SpawnNpcTribe is currently used to recreate missing NPC tribes on guests. This is an overlay, not four independent player tribes. Required work: create/register native player tribes, assign each player's citizens/tools, allocate spawn points near the host, replicate each tribe and preserve its hut/food through reconnect and save/load. The local player-tribe pointer and native UI/AI integration must be resolved before enabling this as playable.

### Civilization

Current civ.cpp assigns owner overlays to cities and vehicles inside one native player cCivilization. That does not create separate political entities or economies. Required work: create/register native civilizations with valid political IDs and native city/vehicle bookkeeping; create one initial city per player through the game's valid city-generation path. Expand map width and height to approximately 2x, enlarge the minimap and ensure enough city sites and sensible travel distances. Never fabricate political IDs or raw native objects from guessed layouts.

### Space

The current Space modules include player rosters, partner-colony mirrors, trade, missions, diplomacy and a legacy player-rivalry path. The target is one shared starting system with up to four equivalent home planets. Required work: deterministic planet allocation/creation, home-empire assignment, cross-planet travel and save/rejoin persistence.

## Portable contract added in this change

rivals_state.h now declares StageLayout for the five stage layouts and assigns the first four existing cities to players 0–3, rather than silently sharing the capital. This is an allocation contract only: it does not create missing native cities, tribes, civilizations or planets.

rivals_tests.cpp checks the stage-to-layout mapping and deterministic city ownership. Portable tests do not replace in-game multiplayer verification.

## Safe implementation sequence

1. Cell: verify current two-to-four-player behavior and repair missing per-player spawn/sync.
2. Creature: verify a single shared nest with all player species and eggs.
3. Tribe: prototype native cTribe creation/registration in a disposable save, then move one test player's population and validate AI, UI, food and save/load before scaling to four.
4. Civilization: identify and verify the native civilization factory/registration and political-ID allocator; create a disposable second civilization and transfer a test city using native bookkeeping.
5. Map: prototype world-size and minimap changes separately; verify terrain, camera, pathfinding and city-site generation.
6. Space: allocate four equivalent home planets in one system; test travel, ownership, persistence and rejoin.
7. Run complete 2-, 3- and 4-client playthroughs across stage transitions.

## Acceptance criteria

Not complete until every stage passes real multi-process tests with 2, 3 and 4 clients; each player controls only their own objects; all clients see the same authoritative world; NPC combat works; reconnect and save/load preserve ownership; and solo play is unaffected.
