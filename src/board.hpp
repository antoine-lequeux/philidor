#pragma once

#include <array>
#include <expected>
#include <iostream>

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
public:

    void make_move(Move mv);
    void unmake_move(Move mv);

    static Board from_startpos();
    static std::expected<Board, std::string> from_fen(std::string fen);

    void display(bool white_perspective = true) const;

private:

    Board() : pieces({}), piece_bb({}), color_bb({}), occupancy(0), side_to_move(Color::WHITE), ply(0), history({}) {}

    std::array<Piece, 64> pieces;
    std::array<Bitboard, 12> piece_bb;
    std::array<Bitboard, 2> color_bb;
    std::array<Bitboard, 2> ortho_sliders;
    std::array<Bitboard, 2> diag_sliders;
    Bitboard occupancy;
    Color side_to_move;
    usize ply;
    std::array<State, 512> history;

    void put_piece(Piece p, Square sq) noexcept;
    void remove_piece(Piece p, Square sq) noexcept;
    void move_piece(Piece p, Square from, Square to) noexcept;
};