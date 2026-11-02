// Portable tests for the multi-stage Rivals session-state foundation.
#include <cstdio>

#include "rivals_state.h"
#include "net/shared_ledger.h"

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static void test_stage_modes()
{
    rivals::SessionState s;
    CHECK(s.stage_mode(rivals::Stage::Cell) == rivals::StageMode::Together);
    CHECK(s.set_stage_mode(rivals::Stage::Tribe, rivals::StageMode::Rivals));
    CHECK(s.stage_mode(rivals::Stage::Tribe) == rivals::StageMode::Rivals);
    CHECK(!s.set_stage_mode(static_cast<rivals::Stage>(99), rivals::StageMode::Rivals));
    CHECK(!s.set_stage_mode(rivals::Stage::Tribe, static_cast<rivals::StageMode>(99)));
}

static void test_relations()
{
    rivals::SessionState s;
    CHECK(s.get_relation(0, 1) == rivals::Relation::Neutral);
    CHECK(s.set_relation(0, 2, rivals::Relation::War));
    CHECK(s.get_relation(0, 2) == rivals::Relation::War);
    CHECK(s.get_relation(2, 0) == rivals::Relation::War); // pair is symmetric
    CHECK(!s.set_relation(1, 1, rivals::Relation::Ally));
    CHECK(!s.set_relation(0, 4, rivals::Relation::Ally));
    CHECK(!s.set_relation(0, 1, static_cast<rivals::Relation>(99)));
    s.clear_player(2);
    CHECK(s.get_relation(0, 2) == rivals::Relation::Neutral);
    CHECK(s.get_relation(2, 0) == rivals::Relation::Neutral);
}

static void test_player_combat_policy()
{
    rivals::SessionState s;
    CHECK(!rivals::allows_player_attack(s, 0, 1)); // neutral is not war
    CHECK(s.set_relation(0, 1, rivals::Relation::Ally));
    CHECK(!rivals::allows_player_attack(s, 0, 1));
    CHECK(!rivals::allows_player_attack(s, 1, 0)); // symmetric relation
    CHECK(s.set_relation(0, 1, rivals::Relation::War));
    CHECK(rivals::allows_player_attack(s, 0, 1));
    CHECK(rivals::allows_player_attack(s, 1, 0));
    CHECK(!rivals::allows_player_attack(s, 0, 0));
    CHECK(!rivals::allows_player_attack(s, 0, 4));
    CHECK(!rivals::allows_player_attack(s, 4, 0));
}

static void test_rivals_balance_merge_ledger()
{
    // A guest's local Tribe/Civ balance is sent once as a contribution to the host total.
    net::SharedLedger guest;
    const double local_balance = 73.5;
    guest.pending = local_balance;
    const uint32_t seq = guest.send();
    CHECK(seq == 1);
    CHECK(guest.pending == 0);
    CHECK(guest.in_flight.size() == 1);


    const double host_before = 120.0;
    const double host_after = net::shared_add(host_before, guest.in_flight[0].v, true);
    CHECK(host_after == 193.5);

    // The host echoes the sequence in MSG_RES. The guest drops the in-flight contribution
    // after seeing that acknowledgement and does not add it again on the next total.
    CHECK(guest.shown(host_after, true, seq) == host_after);
    CHECK(guest.in_flight.empty());
    CHECK(guest.shown(host_after, true, seq) == host_after);
}

static void test_wire_roundtrip_and_validation()
{
    rivals::SessionState source;
    source.set_stage_mode(rivals::Stage::Cell, rivals::StageMode::Rivals);
    source.set_stage_mode(rivals::Stage::Civilization, rivals::StageMode::Rivals);
    source.set_relation(0, 1, rivals::Relation::Ally);
    source.set_relation(1, 3, rivals::Relation::War);

    net::Writer w;
    rivals::write_state(w, source);
    CHECK(w.size() == 12);

    rivals::SessionState decoded;
    net::Reader good(w.data.data(), w.size());
    CHECK(rivals::read_state(good, decoded));
    CHECK(decoded.stage_mode(rivals::Stage::Cell) == rivals::StageMode::Rivals);
    CHECK(decoded.stage_mode(rivals::Stage::Creature) == rivals::StageMode::Together);
    CHECK(decoded.get_relation(0, 1) == rivals::Relation::Ally);
    CHECK(decoded.get_relation(1, 3) == rivals::Relation::War);

    auto bad_bytes = w.data;
    bad_bytes[0] = 99; // unknown wire version
    net::Reader bad_version(bad_bytes.data(), bad_bytes.size());
    CHECK(!rivals::read_state(bad_version, decoded));

    bad_bytes = w.data;
    bad_bytes[1] = 99; // invalid stage mode
    net::Reader bad_mode(bad_bytes.data(), bad_bytes.size());
    CHECK(!rivals::read_state(bad_mode, decoded));
    CHECK(decoded.stage_mode(rivals::Stage::Cell) == rivals::StageMode::Rivals); // failure is atomic

    bad_bytes = w.data;
    bad_bytes[6] = 99; // invalid player relation
    net::Reader bad_relation(bad_bytes.data(), bad_bytes.size());
    CHECK(!rivals::read_state(bad_relation, decoded));
    CHECK(decoded.get_relation(1, 3) == rivals::Relation::War);

    bad_bytes = w.data;
    bad_bytes.push_back(0); // trailing bytes are not part of version 1
    net::Reader trailing(bad_bytes.data(), bad_bytes.size());
    CHECK(!rivals::read_state(trailing, decoded));

    net::Reader truncated(w.data.data(), w.size() - 1);
    CHECK(!rivals::read_state(truncated, decoded));
    CHECK(decoded.get_relation(1, 3) == rivals::Relation::War);
}

int main()
{
    test_stage_modes();
    test_relations();
    test_player_combat_policy();
    test_rivals_balance_merge_ledger();
    test_wire_roundtrip_and_validation();
    std::printf("rivals_tests: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
