#pragma once

#include <array>
#include <expected>
#include <string_view>

#include "defines.hpp"

struct State
{
    CastlingRights castling_rights;
    Square ep_square;
    Piece captured_piece;
    Piece moved_piece;
    u16 halfmove_clock;
};

struct Board
{
    void make_move(Move mv);
    void unmake_move(Move mv);

    static Board from_startpos();
    static std::expected<Board, std::string> from_fen(std::string_view fen);

    void display(bool white_perspective = true) const;

    std::array<Piece, 64> pieces {};
    std::array<Square, 2> kings {};
    std::array<Bitboard, 12> piece_bb {};
    std::array<Bitboard, 2> color_bb {};
    std::array<Bitboard, 2> ortho_sliders {};
    std::array<Bitboard, 2> diag_sliders {};
    Bitboard occupancy = 0;
    Color side_to_move = Color::WHITE;
    u32 ply = 0;
    std::array<State, 512> history {};

private:

    void put_piece(Piece p, Square sq);
    void remove_piece(Piece p, Square sq);
    void move_piece(Piece p, Square from, Square to);
};