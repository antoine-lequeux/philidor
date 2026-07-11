#pragma once

#include <array>

#include "defines.hpp"

constexpr usize MAX_MOVES = 218;

class MoveList
{
public:

    constexpr MoveList() = default;

    void push_back(Move mv)
    {
        [[assume(list_size < MAX_MOVES - 1)]];
        moves[list_size++] = mv;
    }

    Move operator[](usize index) const
    {
        [[assume(index < MAX_MOVES)]];
        return moves[index];
    }

    Move& operator[](usize index)
    {
        [[assume(index < MAX_MOVES)]];
        return moves[index];
    }

    void clear() { list_size = 0; }

    usize size() const { return list_size; }
    bool empty() const { return list_size == 0; }

    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + list_size; }

    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + list_size; }

private:

    std::array<Move, MAX_MOVES> moves;
    u32 list_size = 0;
};