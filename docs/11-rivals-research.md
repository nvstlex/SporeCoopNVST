# 11. Rivals: reverse-engineering findings

Status: research for the next implementation phase. Based on the existing co-op code and
`docs/re/tribe.md` / `docs/re/civ.md`. This document separates verified existing behaviour
from proposed experiments; it does not claim that independent factions are implemented.

## 1. Existing ownership is an overlay, not native faction ownership

### Tribe

The current co-op implementation obtains the single player tribe through
`cGameNounManager::GetPlayerTribe` (the noun manager's `+0x70` pointer). On the host,
`tribe::host_tick` assigns each member of that tribe a network owner ID in `g_hm`. Guests
mirror those members and retain the owner ID. Selection and orders are filtered by owner.

Important consequences:

* Every player's citizens still have the same `cCreatureCitizen::mpOwnerTribe` pointer
  (`+0x1014`), and the game's player-tr tribe remains one object.
* The host command handler explicitly requires a commanded citizen's owner to equal the sender
  and its `mpOwnerTribe` to equal the one player tribe.
* The existing `SpawnNpcTribe` symbol is used to recreate NPC tribes missing on guest copies.
  It is not currently used to make a player's independent tribe.
* `cTribe::AddMember` and `RemoveMember` are available; their verified vtable slots are
  `0x98` and `0x9C`. The RE notes verify the member vector at `+0x340`, selectable
  vector at `+0x354`, hut at `+0x368`, tools at `+0x36C`, and archetype at `+0x550`.

**Conclusion:** assigning different owner IDs is insufficient. A real rival tribe needs a
separate `cTribe` object with its own hut, food container, tools, chief and citizen membership.
The guest's game must also know which tribe is its local player tribe; changing only the host's
citizen ownership table cannot achieve that.

### Civilization

The current co-op implementation gets one player civilization via
`GetPlayerCivilization`. `civ::host_assign_cities` assigns player IDs to cities in
`g_city_owner`, while vehicles inherit the owner of the city that built them. The city owner
table is transmitted to guests, and captured cities are replayed using `cCity::ChangeOwner`.

Important verified facts from `docs/re/civ.md`:

* `GetAllCivilizations` and `GetCivilizationByPoliticalID` expose the world's civilization
  objects.
* `cCivilization` stores its political ID at `+0x30`, wealth at `+0x98`, cities at
  `+0x9C`, vehicles at `+0xB0`, and selectable objects at `+0xD4`.
* `cCity::SetCivilization` (0xBE1280) sets the city pointer and political ID but is explicitly
  a low-level operation without bookkeeping.
* `cCity::ChangeOwner` (0xBE8A00) is the native ownership transition and updates more state,
  including trade-route cleanup and capture bookkeeping.
* The current co-op snapshot records city owner IDs separately from the city's actual political
  ID. That is why the UI can present a city as belonging to a co-op player without creating a
  separate nation.

**Conclusion:** a real rival nation needs a real `cCivilization` object and a valid political
ID, with cities and vehicles attached through native bookkeeping. Merely writing a different
`g_city_owner` value does not create an independent economy, AI planner, culture set or nation.

## 2. Read-only Tribe inventory diagnostic

The Tribe debug command now includes `coop.tribe factions`. It enumerates live
`cTribe` nouns on the current game copy and prints:

* native object address and whether it is the local `GetPlayerTribe` result;
* world position and current member-vector count;
* native hut pointer and food returned by the verified `GetTotalFood` vtable slot;
* archetype and the address of the tribe's food-container field.

Run it once on the host and once on a guest in the same disposable world. Compare the
counts, positions, player markers, hut pointers and food. Pointers are process-local and
must not be compared as identities across machines; use position/archetype/member count
to correlate likely counterparts. The command is read-only: it does not create tribes,
move citizens, or change the player-tribe pointer.

## 3. Tribe combat relation hook

The first gameplay consumer of the shared relation table is now connected. In Tribe mode,
when the stage setting is Rivals, CitizenAction 7 (Attack) against a tracked player-owned
citizen is allowed only if the attacker and target have a War relation. Neutral and Ally
relations block the attack. The host applies the same check to guest commands and to its own
local selection before the original game action function is called. This filters orders only:
all members still reference the same native `cTribe`, so the game may still treat them as one
tribe and suppress actual PvP damage. NPC citizens, unknown owners, non-attack actions and
Together mode retain the existing game path.

The portable policy is covered by `test_player_combat_policy` in
`tests/rivals_tests.cpp`. Runtime verification still needs a two-player Tribe world: verify
neutral/ally rejection, War acceptance in both directions, and that NPC attacks and Together
mode are unchanged. This hook does not create separate native tribes and does not yet change
NPC AI or NPC diplomacy.

## 4. Civilization co-op cities

Civilization is not a player-vs-player Rivals stage in the current co-op design. All connected
players remain members of the same native player civilization and cooperate against NPC nations.
Do not create separate cCivilization objects or allocate political IDs for this mode.

The existing implementation already provides the main building blocks:

* host_assign_cities assigns city-owner overlays to up to four session players. If there are
  fewer cities than players, the capital is shared; once there are enough cities, the capital is
  assigned to player 0 and the remaining cities are distributed to the other players.
* Vehicles inherit the owner of the city that produced them. The host validates guest vehicle
  commands and simulates the authoritative world; guests mirror the cities, buildings, vehicles
  and ownership state.
* City menus prevent a player from buying vehicles or editing a city owned by another player.
  The shared capital remains available while the team has fewer cities than players.
* Captures are attributed to the closest player's vehicles, so a captured NPC city joins that
  player's city group.
* The shared treasury and existing joint-siege bonus reward coordinated attacks on NPC cities.

The Civilization Rivals attack-order gate was removed. It was conceptually wrong for this mode:
it blocked attacks on teammates instead of making the whole party fight NPC civilizations
together. effective_stage_mode now always reports Civilization as Together, and the lobby rejects
attempts to switch it to Rivals. The native city/vehicle ownership overlay is sufficient for the
current requirement; separate-native-civilization research remains relevant only if a future
design explicitly calls for independent nations.

Runtime verification should focus on four-player co-op: one city with four players (shared
capital), then two/three/four player cities; city ownership assignment; each player's city menu
and vehicle control; joint attacks on NPC cities; captured-city attribution; reconnect and world
reload. Verify that no attack order against an NPC is blocked.

## 5. Space stage: existing Rivals system

Space is not an empty stage. `space_rivalry.cpp` already implements a dedicated multiplayer
Rivals system:

- the host switches Allies/Rivals only while the players are in Space;
- entering Rivals splits the current shared empire money across players, while returning to
  Allies merges the last reported budgets and reconciles each player's unreported delta;
- a guest who rejoins or reloads asks the host to restore its last budget by player name;
- alliances and peace require proposals/acceptance; war is declared directly;
- the host stores relations by player-name pairs so a reconnect with a changed network ID can
  recover them;
- hostile ship puppets receive a neutral political ID so weapons can hit them; damage is routed
  to the owning player's real ship and applied there. Neutral/allied puppets retain the local
  empire political ID and are protected by the game's friendly-fire rules.

This system deliberately remains separate from the generic cross-stage relation table: it has
its own name-based persistence, budget protocol, Space UI and damage-routing path. The general
lobby currently allows a Space mode switch only while Space is active, because that transition
must execute the Space-specific money split/merge. A future-stage Space selection and one
unified relation API require a separate integration pass; do not replace the existing Space
protocol with the generic matrix without preserving reconnect restoration and damage routing.

Runtime verification should cover two players through split, income/spending, alliance, war,
ship damage, peace, merge and reconnect/reload budget restoration. Existing debug entry point:
`coop.space rivalry` (status, mode, ally, accept, war, peace, and diagnostic counters).

## 6. Safe implementation order

Do not switch the lobby to offer a stage as fully playable Rivals until its native faction
prototype survives a two-process test.

### Tribe prototype

1. Use the read-only `coop.tribe factions` diagnostic on the host and a guest to inventory
   every `cTribe`: object address, position, member count, hut, food, archetype and player marker.
2. In a disposable test world, create one empty NPC tribe with `SpawnNpcTribe` at a
   preselected NPC village position. Do not move citizens or alter the player pointer yet.
3. Verify that the new tribe appears in the game's tribe list, has a valid hut/food container,
   survives several minutes of simulation, and is reproduced on a guest by the existing
   tribe-state path.
4. Only after that, prototype moving one test citizen with the native `RemoveMember` /
   `AddMember` path. Verify `mpOwnerTribe`, selectable membership, AI, food and save/load.
5. Determine whether the game has a supported way to set the local player's tribe pointer
   independently on each client. If not, stop: a true second player tribe cannot be made by
   member reassignment alone.

### Civilization prototype

1. Use the read-only `coop.civ factions` inventory on host and guest to compare native
   political IDs, player-owned flags, wealth, city counts, vehicle counts and culture-set pointers.
2. Find the game's native civilization creation/registration path and determine how a new
   political ID is allocated. Do not fabricate a political ID.
3. In a disposable world, create/register a second civilization with no cities and verify that
   `GetCivilizationByPoliticalID` resolves it and that it survives save/load.
4. Transfer one non-capital test city using the native ownership path. Check both civilizations'
   city vectors, political IDs, AI, city hall, trade routes and guest snapshots.
5. Verify how each guest's `GetPlayerCivilization` can point to its own nation. If this is not
   supported, do not advertise independent civilization gameplay.

## 7. Current blockers and non-goals

* The current branch's per-player Tribe food and Civilization money are local-copy balances, not
  balances stored on independent native factions.
* The generic player relation matrix is session state only. Tribe and Civilization attack-order
  hooks consult it, but NPC AI, NPC diplomacy, non-order damage and full native city capture do not.
* The current city/member owner maps are still useful infrastructure and should be retained;
  they provide network identity, command validation and overlays after native faction ownership
  is introduced.
* Never mutate native faction pointers or political IDs based only on guessed offsets. Use the
  verified RE notes, then a disposable runtime probe and a two-process regression test.
* Keep the current Together behaviour unchanged. Rivals-specific hooks must be gated by the
  selected stage mode and have a safe fallback when the prototype is unavailable.

## 8. Decision

The next code milestone is **a diagnostic and disposable native-faction prototype**, not a
premature implementation of the full stage. Tribe is the first candidate because the mod already
has citizen IDs, owner-aware orders, member creation/removal and NPC tribe mirroring. Civilization
follows after the native civilization creation and player-pointer path is understood.
