#pragma once

#include <array>
#include <cassert>

#include "defines.hpp"

constexpr usize MAX_MOVES = 218;

struct ScoredMove
{
    Move move;
    Score score;

    constexpr ScoredMove(Move m) : move(m), score(0) {}
    constexpr ScoredMove() = default;
};

class MoveList
{
public:

    constexpr MoveList() = default;

    inline void push_back(Move mv)
    {
        [[assume(list_size < MAX_MOVES)]];
        moves[list_size++] = {mv};
    }

    inline ScoredMove operator[](usize index) const
    {
        [[assume(index < MAX_MOVES)]];
        return moves[index];
    }

    inline ScoredMove& operator[](usize index)
    {
        [[assume(index < MAX_MOVES)]];
        return moves[index];
    }

    inline void clear() { list_size = 0; }

    inline void pop_back()
    {
        [[assume(list_size > 0)]];
        list_size--;
    }

    inline void remove(usize index)
    {
        [[assume(index < list_size)]];
        moves[index] = moves[--list_size];
    }

    inline usize size() const { return list_size; }
    inline bool empty() const { return list_size == 0; }

    inline const ScoredMove* begin() const { return moves.data(); }
    inline const ScoredMove* end() const { return moves.data() + list_size; }

    inline ScoredMove* begin() { return moves.data(); }
    inline ScoredMove* end() { return moves.data() + list_size; }

private:

    std::array<ScoredMove, MAX_MOVES> moves;
    u32 list_size = 0;
};