#pragma once

#include "board.hpp"
#include "movelist.hpp"

inline constexpr usize bb_index(Type type, usize color_idx) noexcept
{
    [[assume(static_cast<u8>(type) >= 1 && static_cast<u8>(type) <= 6)]];
    [[assume(color_idx < 2)]];
    return (static_cast<usize>(type) - 1) + 6 * color_idx;
}

inline constexpr usize bb_index(Type type, Color color) noexcept { return bb_index(type, static_cast<usize>(color)); }

void generate_moves(const Board& board, MoveList& ml) noexcept;