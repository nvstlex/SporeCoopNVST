// Portable foundation for the multi-stage Rivals mode (docs/11-rivals.md).
// This is deliberately game-agnostic: ownership and Spore object hooks are layered on later.
#pragma once

#include <array>
#include <cstdint>

#include "net/buffer.h"

namespace rivals {

constexpr uint8_t kMaxPlayers = 4;
constexpr uint8_t kStageCount = 5;

enum class Stage : uint8_t { Cell = 0, Creature, Tribe, Civilization, Space };
enum class StageMode : uint8_t { Together = 0, Rivals = 1 };
enum class Relation : uint8_t { Neutral = 0, Ally = 1, War = 2 };

struct SessionState {
    std::array<StageMode, kStageCount> stage_modes{};
    std::array<std::array<Relation, kMaxPlayers>, kMaxPlayers> relations{};

    SessionState() { reset(); }

    void reset()
    {
        stage_modes.fill(StageMode::Together);
        for (auto& row : relations)
            row.fill(Relation::Neutral);
    }

    bool set_stage_mode(Stage stage, StageMode mode)
    {
        const auto s = static_cast<uint8_t>(stage);
        const auto m = static_cast<uint8_t>(mode);
        if (s >= kStageCount || m > static_cast<uint8_t>(StageMode::Rivals))
            return false;
        stage_modes[s] = mode;
        return true;
    }

    StageMode stage_mode(Stage stage) const
    {
        const auto s = static_cast<uint8_t>(stage);
        return s < kStageCount ? stage_modes[s] : StageMode::Together;
    }

    bool set_relation(uint8_t a, uint8_t b, Relation relation)
    {
        const auto r = static_cast<uint8_t>(relation);
        if (a >= kMaxPlayers || b >= kMaxPlayers || a == b || r > static_cast<uint8_t>(Relation::War))
            return false;
        relations[a][b] = relation;
        relations[b][a] = relation;
        return true;
    }

    Relation get_relation(uint8_t a, uint8_t b) const
    {
        if (a >= kMaxPlayers || b >= kMaxPlayers || a == b)
            return Relation::Neutral;
        return relations[a][b];
    }

    void clear_player(uint8_t id)
    {
        if (id >= kMaxPlayers)
            return;
        for (uint8_t other = 0; other < kMaxPlayers; ++other) {
            relations[id][other] = Relation::Neutral;
            relations[other][id] = Relation::Neutral;
        }
    }
};

// Player-vs-player combat is permitted only for an explicit War relation.
// NPCs / unassigned owners are handled by the game rules, not this policy.
inline bool allows_player_attack(const SessionState& state, uint8_t attacker, uint8_t target)
{
    return attacker < kMaxPlayers && target < kMaxPlayers && attacker != target &&
           state.get_relation(attacker, target) == Relation::War;
}

// Implemented by coop.cpp: the authoritative table shared with stage modules.
StageMode current_stage_mode(Stage stage);

// Fixed-size wire format: version, five stage modes, then the six unordered player pairs.
// Decode into a temporary so malformed packets never partially mutate live session state.
inline void write_state(net::Writer& w, const SessionState& state)
{
    w.u8(1);
    for (auto mode : state.stage_modes)
        w.u8(static_cast<uint8_t>(mode));
    for (uint8_t a = 0; a < kMaxPlayers; ++a)
        for (uint8_t b = a + 1; b < kMaxPlayers; ++b)
            w.u8(static_cast<uint8_t>(state.relations[a][b]));
}

inline bool read_state(net::Reader& r, SessionState& out)
{
    if (r.left() != 1 + kStageCount + (kMaxPlayers * (kMaxPlayers - 1)) / 2)
        return false;
    if (r.u8() != 1)
        return false;

    SessionState next;
    for (uint8_t s = 0; s < kStageCount; ++s) {
        const uint8_t mode = r.u8();
        if (mode > static_cast<uint8_t>(StageMode::Rivals))
            return false;
        next.stage_modes[s] = static_cast<StageMode>(mode);
    }
    for (uint8_t a = 0; a < kMaxPlayers; ++a) {
        for (uint8_t b = a + 1; b < kMaxPlayers; ++b) {
            const uint8_t relation = r.u8();
            if (relation > static_cast<uint8_t>(Relation::War))
                return false;
            next.set_relation(a, b, static_cast<Relation>(relation));
        }
    }
    if (!r.ok() || r.left() != 0)
        return false;
    out = next;
    return true;
}

} // namespace rivals
