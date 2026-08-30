#pragma once

#include "defines.hpp"
#include <optional>
#include <vector>

enum class Bound : u8
{
    NONE,
    EXACT,
    LOWER,
    UPPER
};

struct TTEntry
{
    u64 key;
    Move move;
    i16 score;
    i8 depth;
    u8 bound_age; // bound: bits [0:1], age: bits [2:7]

    Bound get_bound() const { return static_cast<Bound>(bound_age & 0x3); }
    u8 get_age() const { return bound_age >> 2; }
    void set_bound_age(Bound b, u8 age) { bound_age = static_cast<u8>(static_cast<u8>(b) | (age << 2)); }
};

static_assert(sizeof(TTEntry) == 16);

class TranspositionTable
{
public:

    TranspositionTable(usize megabytes);

    void clear();
    void new_search();

    void store(u64 key, i32 depth, i32 ply, Score score, Bound bound, Move best_move);
    std::optional<TTEntry> probe(u64 key, i32 ply) const;
    usize get_hashfull() const;

private:

    std::vector<TTEntry> table;
    usize num_entries;
    u8 current_age;
};
