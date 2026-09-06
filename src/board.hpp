#pragma once

#include <array>
#include <expected>
#include <memory>
#include <string_view>

#include "defines.hpp"
#include "movelist.hpp"
#include "nnue.hpp"
#include "zobrist.hpp"

struct State
{
    CastlingRights castling_rights;
    Square ep_square;
    Piece captured_piece;
    Piece moved_piece;
    u16 halfmove_clock;
    Move move;
    u64 hash;
    Accumulator acc;
};

struct Board
{
    Board();
    Board(const Board& other);
    Board& operator=(const Board& other);
    ~Board();

    inline void make_move(Move mv);
    inline void unmake_move(Move mv);

    static Board from_startpos();
    static std::expected<Board, std::string> from_fen(std::string_view fen);

    void display(bool white_perspective = true) const;

    Score evaluate() const;
    template <GenType gt>
    MoveList generate_moves() const;

    void make_null();
    void unmake_null();

    bool has_non_pawn_material(Color c) const
    {
        return (piece_bb[bb_index(Type::KNIGHT, c)] | piece_bb[bb_index(Type::BISHOP, c)] |
                piece_bb[bb_index(Type::ROOK, c)] | piece_bb[bb_index(Type::QUEEN, c)]) != 0;
    }

    u64 zobrist_key() const { return (*history)[ply].hash; }
    bool in_check() const;
    bool is_pseudo_legal(Move m) const;
    bool is_legal(Move m);
    constexpr bool is_capture(Move m) const
    {
        return pieces[m.get_target_square()] != EMPTY || m.get_flag() == Move::ENPASSANT_CAPTURE_FLAG;
    }
    constexpr bool is_quiet(Move m) const { return !is_capture(m) && !m.is_promotion(); }
    bool is_draw(i32 search_ply = 0) const;

    Bitboard occupied_by(Color color, Bitboard occ = ~0ULL) const;
    Bitboard attackers_to(Square sq, Bitboard occ) const;
    Bitboard pieces_of_type(Type type) const;

    std::array<Piece, 64> pieces {};
    std::array<Square, 2> kings {};
    std::array<Bitboard, 12> piece_bb {};
    std::array<Bitboard, 2> color_bb {};
    std::array<Bitboard, 2> ortho_sliders {};
    std::array<Bitboard, 2> diag_sliders {};
    Bitboard occupancy = 0;
    Color side_to_move = Color::WHITE;
    u32 ply = 0;
    std::unique_ptr<std::array<State, 512>> history;

private:

    inline void put_piece(Piece p, Square sq);
    inline void remove_piece(Piece p, Square sq);
    inline void move_piece(Piece p, Square from, Square to);
};

inline void Board::put_piece(Piece p, Square sq)
{
    pieces[sq] = p;
    const u64 bit = 1ULL << sq;
    const Type pt = get_piece_type(p);
    const usize ci = color_index(get_piece_color(p));

    piece_bb[bb_index(pt, ci)] |= bit;
    color_bb[ci] |= bit;

    if (pt == Type::KING)
        kings[ci] = sq;
    else
    {
        if (pt == Type::ROOK || pt == Type::QUEEN) ortho_sliders[ci] |= bit;
        if (pt == Type::BISHOP || pt == Type::QUEEN) diag_sliders[ci] |= bit;
    }

    occupancy |= bit;
}

inline void Board::remove_piece(Piece p, Square sq)
{
    pieces[sq] = EMPTY;
    const u64 bit = ~(1ULL << sq);
    const Type pt = get_piece_type(p);
    const usize ci = color_index(get_piece_color(p));

    piece_bb[bb_index(pt, ci)] &= bit;
    color_bb[ci] &= bit;

    if (pt == Type::ROOK || pt == Type::QUEEN) ortho_sliders[ci] &= bit;
    if (pt == Type::BISHOP || pt == Type::QUEEN) diag_sliders[ci] &= bit;

    occupancy &= bit;
}

inline void Board::move_piece(Piece p, Square from, Square to)
{
    pieces[from] = EMPTY;
    pieces[to] = p;

    const Type pt = get_piece_type(p);
    const usize ci = color_index(get_piece_color(p));

    const u64 move_mask = (1ULL << from) | (1ULL << to);

    piece_bb[bb_index(pt, ci)] ^= move_mask;
    color_bb[ci] ^= move_mask;

    if (pt == Type::KING)
        kings[ci] = to;
    else
    {
        if (pt == Type::ROOK || pt == Type::QUEEN) ortho_sliders[ci] ^= move_mask;
        if (pt == Type::BISHOP || pt == Type::QUEEN) diag_sliders[ci] ^= move_mask;
    }

    occupancy ^= move_mask;
}

inline constexpr std::array<CastlingRights, 64> CASTLING_UPDATE = [] {
    std::array<CastlingRights, 64> arr {};
    for (auto& rights : arr) rights = CastlingRights::ALL;
    arr[0] = ~CastlingRights::WQ;                         // a1
    arr[4] = ~(CastlingRights::WK | CastlingRights::WQ);  // e1
    arr[7] = ~CastlingRights::WK;                         // h1
    arr[56] = ~CastlingRights::BQ;                        // a8
    arr[60] = ~(CastlingRights::BK | CastlingRights::BQ); // e8
    arr[63] = ~CastlingRights::BK;                        // h8
    return arr;
}();

inline void Board::make_move(Move mv)
{
    [[assume(ply < 511)]];

    const Square from = mv.get_start_square();
    const Square to = mv.get_target_square();
    const u16 flag = mv.get_flag();

    const Piece moved = pieces[from];

    [[assume(moved != EMPTY)]];

    const Type moved_type = get_piece_type(moved);
    const Color us = side_to_move;

    Piece captured = pieces[to];

    State& current_state = (*history)[ply];
    State& next_state = (*history)[ply + 1];

    next_state.castling_rights = current_state.castling_rights;
    next_state.halfmove_clock = current_state.halfmove_clock;
    next_state.ep_square = NO_SQUARE;

    current_state.moved_piece = moved;

    next_state.acc = current_state.acc;

    u64 hash = current_state.hash;
    hash ^= zobrist::get_ep_key(current_state.ep_square);
    hash ^= zobrist::get_castling_key(current_state.castling_rights);
    hash ^= zobrist::get_side_key();

    if (flag == Move::ENPASSANT_CAPTURE_FLAG)
    {
        Square capture_sq = (us == Color::WHITE) ? to - 8 : to + 8;
        captured = pieces[capture_sq];
        remove_piece(captured, capture_sq);
        hash ^= zobrist::get_piece_key(captured, capture_sq);
        NNUE::remove_piece(next_state.acc, color_index(!us), piece_type_index(Type::PAWN), capture_sq);
    }
    else if (captured != EMPTY)
    {
        remove_piece(captured, to);
        hash ^= zobrist::get_piece_key(captured, to);
        NNUE::remove_piece(next_state.acc, color_index(!us), piece_type_index(get_piece_type(captured)), to);
    }
    current_state.captured_piece = captured;

    if (moved_type == Type::PAWN || captured != EMPTY)
        next_state.halfmove_clock = 0;
    else
        next_state.halfmove_clock++;

    move_piece(moved, from, to);
    hash ^= zobrist::get_piece_key(moved, from);
    NNUE::move_piece(next_state.acc, color_index(us), piece_type_index(moved_type), from, to);

    if (flag == Move::CASTLE_FLAG)
    {
        const Piece rook = make_piece(Type::ROOK, us);
        const usize ci = color_index(us);
        const usize pt = piece_type_index(Type::ROOK);

        switch (to)
        {
            case 6:
                move_piece(rook, 7, 5);
                hash ^= zobrist::get_piece_key(rook, 7) ^ zobrist::get_piece_key(rook, 5);
                NNUE::move_piece(next_state.acc, ci, pt, 7, 5);
                break; // White Kingside
            case 2:
                move_piece(rook, 0, 3);
                hash ^= zobrist::get_piece_key(rook, 0) ^ zobrist::get_piece_key(rook, 3);
                NNUE::move_piece(next_state.acc, ci, pt, 0, 3);
                break; // White Queenside
            case 62:
                move_piece(rook, 63, 61);
                hash ^= zobrist::get_piece_key(rook, 63) ^ zobrist::get_piece_key(rook, 61);
                NNUE::move_piece(next_state.acc, ci, pt, 63, 61);
                break; // Black Kingside
            case 58:
                move_piece(rook, 56, 59);
                hash ^= zobrist::get_piece_key(rook, 56) ^ zobrist::get_piece_key(rook, 59);
                NNUE::move_piece(next_state.acc, ci, pt, 56, 59);
                break; // Black Queenside
        }
        hash ^= zobrist::get_piece_key(moved, to);
    }
    else if (mv.is_promotion())
    {
        const Piece promoted = make_piece(mv.get_promotion_type(), us);
        remove_piece(moved, to);
        put_piece(promoted, to);
        hash ^= zobrist::get_piece_key(promoted, to);
        NNUE::remove_piece(next_state.acc, color_index(us), piece_type_index(Type::PAWN), to);
        NNUE::add_piece(next_state.acc, color_index(us), piece_type_index(mv.get_promotion_type()), to);
    }
    else
    {
        if (flag == Move::PAWN_TWO_UP_FLAG) next_state.ep_square = (us == Color::WHITE) ? to - 8 : to + 8;
        hash ^= zobrist::get_piece_key(moved, to);
    }

    next_state.castling_rights &= CASTLING_UPDATE[from];
    next_state.castling_rights &= CASTLING_UPDATE[to];

    hash ^= zobrist::get_castling_key(next_state.castling_rights);
    hash ^= zobrist::get_ep_key(next_state.ep_square);

    next_state.hash = hash;

    current_state.move = mv;

    side_to_move = !side_to_move;
    ply++;
}

inline void Board::unmake_move(Move mv)
{
    [[assume(ply > 0)]];

    ply--;
    side_to_move = !side_to_move;

    const Square from = mv.get_start_square();
    const Square to = mv.get_target_square();
    const u16 flag = mv.get_flag();

    const State& state = (*history)[ply];
    const Piece moved = state.moved_piece;
    const Piece captured = state.captured_piece;
    const Color us = side_to_move;

    if (mv.is_promotion())
    {
        remove_piece(pieces[to], to);
        put_piece(moved, to);
    }

    move_piece(moved, to, from);

    if (flag == Move::ENPASSANT_CAPTURE_FLAG)
    {
        Square capture_sq = (us == Color::WHITE) ? to - 8 : to + 8;
        put_piece(captured, capture_sq);
    }
    else if (captured != EMPTY)
    {
        put_piece(captured, to);
    }

    if (flag == Move::CASTLE_FLAG)
    {
        const Piece rook = make_piece(Type::ROOK, us);
        switch (to)
        {
            case 6: move_piece(rook, 5, 7); break;    // White Kingside
            case 2: move_piece(rook, 3, 0); break;    // White Queenside
            case 62: move_piece(rook, 61, 63); break; // Black Kingside
            case 58: move_piece(rook, 59, 56); break; // Black Queenside
        }
    }
}
