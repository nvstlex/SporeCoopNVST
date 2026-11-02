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

## 3. Safe implementation order

Do not switch the lobby to offer a stage as fully playable Rivals until its native faction
prototype survives a two-process test.

### Tribe prototype

1. Add a host-only diagnostic that inventories every `cTribe`: object address, position,
   member count, hut, archetype and whether it is the current player tribe.
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

1. Inventory `GetAllCivilizations`: political ID, player-owned flag, wealth, city count,
   vehicle count and culture-set pointer.
2. Find the game's native civilization creation/registration path and determine how a new
   political ID is allocated. Do not fabricate a political ID.
3. In a disposable world, create/register a second civilization with no cities and verify that
   `GetCivilizationByPoliticalID` resolves it and that it survives save/load.
4. Transfer one non-capital test city using the native ownership path. Check both civilizations'
   city vectors, political IDs, AI, city hall, trade routes and guest snapshots.
5. Verify how each guest's `GetPlayerCivilization` can point to its own nation. If this is not
   supported, do not advertise independent civilization gameplay.

## 4. Current blockers and non-goals

* The current branch's per-player Tribe food and Civilization money are local-copy balances, not
  balances stored on independent native factions.
* The generic player relation matrix is session state only; it does not currently affect NPC AI,
  citizen combat or city capture.
* The current city/member owner maps are still useful infrastructure and should be retained;
  they provide network identity, command validation and overlays after native faction ownership
  is introduced.
* Never mutate native faction pointers or political IDs based only on guessed offsets. Use the
  verified RE notes, then a disposable runtime probe and a two-process regression test.
* Keep the current Together behaviour unchanged. Rivals-specific hooks must be gated by the
  selected stage mode and have a safe fallback when the prototype is unavailable.

## 5. Decision

The next code milestone is **a diagnostic and disposable native-faction prototype**, not a
premature implementation of the full stage. Tribe is the first candidate because the mod already
has citizen IDs, owner-aware orders, member creation/removal and NPC tribe mirroring. Civilization
follows after the native civilization creation and player-pointer path is understood.
