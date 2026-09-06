#include "tt.hpp"
#include <algorithm>
#include <cstring>

static Score score_to_tt(Score score, i32 ply)
{
    if (score >= MATE_VALUE - 1000) return score + ply;
    if (score <= -MATE_VALUE + 1000) return score - ply;
    return score;
}

static Score score_from_tt(Score score, i32 ply)
{
    if (score >= MATE_VALUE - 1000) return score - ply;
    if (score <= -MATE_VALUE + 1000) return score + ply;
    return score;
}

TranspositionTable::TranspositionTable(usize megabytes)
{
    usize bytes = megabytes * 1024 * 1024;
    num_entries = bytes / sizeof(TTEntry);
    if (num_entries == 0) num_entries = 1;
    table.resize(num_entries);
    clear();
}

void TranspositionTable::clear()
{
    std::memset(table.data(), 0, num_entries * sizeof(TTEntry));
    current_age = 0;
}

void TranspositionTable::new_search()
{
    current_age = static_cast<u8>((current_age + 1) & 0x3F);
}

void TranspositionTable::store(u64 key, i32 depth, i32 ply, Score score, Bound bound, Move best_move)
{
    usize index = key % num_entries;
    TTEntry* entry = &table[index];

    bool replace = false;

    if (entry->key == key)
    {
        entry->set_bound_age(entry->get_bound(), current_age);
        if (entry->move.is_null() && !best_move.is_null()) entry->move = best_move;
        if (depth >= entry->depth) replace = true;
    }
    else
    {
        if (entry->get_age() != current_age || depth >= entry->depth) replace = true;
    }

    if (replace || entry->get_bound() == Bound::NONE)
    {
        bool was_empty = entry->get_bound() == Bound::NONE;
        entry->key = key;
        entry->score = static_cast<i16>(score_to_tt(score, ply));
        entry->depth = static_cast<i8>(depth);
        entry->set_bound_age(bound, current_age);

        if (!best_move.is_null() || was_empty) entry->move = best_move;
    }
}

std::optional<TTEntry> TranspositionTable::probe(u64 key, i32 ply) const
{
    usize index = key % num_entries;
    const TTEntry* entry = &table[index];
    if (entry->key == key && entry->get_bound() != Bound::NONE)
    {
        TTEntry copy = *entry;
        copy.score = static_cast<i16>(score_from_tt(copy.score, ply));
        return copy;
    }
    return std::nullopt;
}

usize TranspositionTable::get_hashfull() const
{
    usize used = 0;
    usize sample_size = std::min(num_entries, static_cast<usize>(1000));
    for (usize i = 0; i < sample_size; ++i)
        if (table[i].get_bound() != Bound::NONE) used++;
    return (used * 1000) / sample_size;
}
