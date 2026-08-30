#pragma once

#include "defines.hpp"
struct Board;

namespace zobrist
{
void init();

u64 get_piece_key(Piece p, Square sq);
u64 get_castling_key(CastlingRights cr);
u64 get_ep_key(Square sq);
u64 get_side_key();

u64 compute_hash(const Board& board);
} // namespace zobrist
