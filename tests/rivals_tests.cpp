// Portable tests for the multi-stage Rivals session-state foundation.
#include <cstdio>

#include "rivals_state.h"

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

    bad_bytes = w.data;
    bad_bytes[6] = 99; // invalid player relation
    net::Reader bad_relation(bad_bytes.data(), bad_bytes.size());
    CHECK(!rivals::read_state(bad_relation, decoded));

    net::Reader truncated(w.data.data(), w.size() - 1);
    CHECK(!rivals::read_state(truncated, decoded));
}

int main()
{
    test_stage_modes();
    test_relations();
    test_wire_roundtrip_and_validation();
    std::printf("rivals_tests: %s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
