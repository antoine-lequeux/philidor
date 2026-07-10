#pragma once

#include <array>
#include <cassert>

#include "defines.hpp"

inline constexpr usize MAX_MOVES = 218;

class MoveList
{
public:

    constexpr MoveList() noexcept : list_size(0) {}

    inline void push_back(Move mv) noexcept
    {
        [[assume(list_size < MAX_MOVES - 1)]];
        moves[list_size++] = mv;
    }

    inline Move operator[](usize index) const noexcept
    {
        [[assume(index < MAX_MOVES)]];
        return moves[index];
    }

    inline Move& operator[](usize index) noexcept
    {
        [[assume(index < MAX_MOVES)]];
        return moves[index];
    }

    inline void clear() noexcept { list_size = 0; }

    inline usize size() const noexcept { return static_cast<usize>(list_size); }
    inline bool empty() const noexcept { return list_size == 0; }

    inline const Move* begin() const noexcept { return moves.data(); }
    inline const Move* end() const noexcept { return moves.data() + list_size; }

    inline Move* begin() noexcept { return moves.data(); }
    inline Move* end() noexcept { return moves.data() + list_size; }

private:

    std::array<Move, MAX_MOVES> moves;
    u16 list_size = 0;
};