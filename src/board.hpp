#pragma once

#include <array>

#include "defines.hpp"

struct State
{
    CastlingRights castling_rights;
    Square ep_square;
    Piece captured_piece;
    Piece moved_piece;
    u16 halfmove_clock;
};

class Board
{
    std::array<Piece, 64> pieces;
    std::array<Bitboard, 12> piece_bb;
    std::array<Bitboard, 2> color_bb;
    Bitboard occupancy;
    Color side_to_move;
    usize ply;
    std::array<State, 512> history;
};