#include "movegen.hpp"
#include "magic.hpp"
#include "masks.hpp"

namespace
{
template <Color US>
constexpr u64 shift_pawn_up(u64 bb) noexcept
{
    return (US == Color::WHITE) ? (bb << 8) : (bb >> 8);
}

template <Color US>
constexpr u64 shift_pawn_west(u64 bb) noexcept
{
    return (US == Color::WHITE) ? ((bb & ~FILE_A) << 7) : ((bb & ~FILE_A) >> 9);
}

template <Color US>
constexpr u64 shift_pawn_east(u64 bb) noexcept
{
    return (US == Color::WHITE) ? ((bb & ~FILE_H) << 9) : ((bb & ~FILE_H) >> 7);
}

template <Color US>
constexpr Square pawn_from_up(Square to) noexcept
{
    return (US == Color::WHITE) ? to - 8 : to + 8;
}
template <Color US>
constexpr Square pawn_from_up_2(Square to) noexcept
{
    return (US == Color::WHITE) ? to - 16 : to + 16;
}
template <Color US>
constexpr Square pawn_from_west(Square to) noexcept
{
    return (US == Color::WHITE) ? to - 7 : to + 9;
}
template <Color US>
constexpr Square pawn_from_east(Square to) noexcept
{
    return (US == Color::WHITE) ? to - 9 : to + 7;
}

inline void add_moves(u64 targets, Square from, MoveList& ml) noexcept
{
    for (Square to : Bitloop(targets))
    {
        ml.push_back(Move::make(from, to));
    }
}

template <Color BY>
inline bool is_attacked_by(const Board& board, Square sq, u64 occ) noexcept
{
    constexpr Color US = !BY;
    constexpr u8 by_idx = static_cast<u8>(BY);

    if (pawn_attacks(sq, US) & board.piece_bb[bb_index(Type::PAWN, by_idx)])
        return true;
    if (knight_attacks(sq) & board.piece_bb[bb_index(Type::KNIGHT, by_idx)])
        return true;
    if (king_attacks(sq) & (1ULL << board.kings[by_idx]))
        return true;

    if (bishop_attacks(sq, occ) & board.diag_sliders[by_idx])
        return true;
    if (rook_attacks(sq, occ) & board.ortho_sliders[by_idx])
        return true;

    return false;
}

template <Color US>
void generate_moves_impl(const Board& board, MoveList& ml) noexcept
{
    constexpr Color THEM = !US;
    constexpr u8 us_idx = static_cast<u8>(US);
    constexpr u8 them_idx = static_cast<u8>(THEM);

    constexpr u64 PROMO_RANK = (US == Color::WHITE) ? RANK_8 : RANK_1;
    constexpr u64 RANK_3_POV = (US == Color::WHITE) ? RANK_3 : RANK_6;

    const Square ksq = board.kings[us_idx];
    const u64 occ = board.occupancy;
    const u64 our = board.color_bb[us_idx];
    const u64 their = board.color_bb[them_idx] & ~(1ULL << board.kings[them_idx]);

    const u64 checkers = (pawn_attacks(ksq, US) & board.piece_bb[bb_index(Type::PAWN, them_idx)]) |
                         (knight_attacks(ksq) & board.piece_bb[bb_index(Type::KNIGHT, them_idx)]) |
                         (bishop_attacks(ksq, occ) & board.diag_sliders[them_idx]) |
                         (rook_attacks(ksq, occ) & board.ortho_sliders[them_idx]);

    const int num_checkers = std::popcount(checkers);

    u64 pinned = 0;
    const u64 pinners = (bishop_attacks(ksq, 0) & board.diag_sliders[them_idx]) |
                        (rook_attacks(ksq, 0) & board.ortho_sliders[them_idx]);

    for (Square pinner_sq : Bitloop(pinners))
    {
        u64 blockers = squares_between(ksq, pinner_sq) & occ;
        if (blockers && std::has_single_bit(blockers) && (blockers & our))
        {
            pinned |= blockers;
        }
    }

    const u64 occ_no_king = occ ^ (1ULL << ksq);
    for (Square to : Bitloop(king_attacks(ksq) & ~our))
    {
        if (!is_attacked_by<THEM>(board, to, occ_no_king))
        {
            ml.push_back(Move::make(ksq, to));
        }
    }

    if (num_checkers > 1)
        return;

    u64 target_mask = ~0ULL;
    if (num_checkers == 1)
    {
        Square checker_sq = static_cast<Square>(std::countr_zero(checkers));
        target_mask = checkers | squares_between(ksq, checker_sq);
    }

    if (num_checkers == 0)
    {
        const CastlingRights cr = board.history[board.ply].castling_rights;
        if constexpr (US == Color::WHITE)
        {
            if (((cr & CastlingRights::WK) != CastlingRights::NONE) && !(occ & WHITE_OO_BLOCKERS) &&
                !is_attacked_by<THEM>(board, 5, occ) && !is_attacked_by<THEM>(board, 6, occ))
            {
                ml.push_back(Move::make(4, 6, Move::CASTLE_FLAG));
            }
            if (((cr & CastlingRights::WQ) != CastlingRights::NONE) && !(occ & WHITE_OOO_BLOCKERS) &&
                !is_attacked_by<THEM>(board, 3, occ) && !is_attacked_by<THEM>(board, 2, occ))
            {
                ml.push_back(Move::make(4, 2, Move::CASTLE_FLAG));
            }
        }
        else
        {
            if (((cr & CastlingRights::BK) != CastlingRights::NONE) && !(occ & BLACK_OO_BLOCKERS) &&
                !is_attacked_by<THEM>(board, 61, occ) && !is_attacked_by<THEM>(board, 62, occ))
            {
                ml.push_back(Move::make(60, 62, Move::CASTLE_FLAG));
            }
            if (((cr & CastlingRights::BQ) != CastlingRights::NONE) && !(occ & BLACK_OOO_BLOCKERS) &&
                !is_attacked_by<THEM>(board, 59, occ) && !is_attacked_by<THEM>(board, 58, occ))
            {
                ml.push_back(Move::make(60, 58, Move::CASTLE_FLAG));
            }
        }
    }

    const u64 not_pinned = ~pinned;

    const u64 pawns = board.piece_bb[bb_index(Type::PAWN, us_idx)];
    const u64 empty = ~occ;
    const u64 free_pawns = pawns & not_pinned;
    const u64 pinned_pawns = pawns & pinned;

    const u64 single = shift_pawn_up<US>(free_pawns) & empty;

    for (Square to : Bitloop(single & PROMO_RANK & target_mask))
    {
        Square from = pawn_from_up<US>(to);
        ml.push_back(Move::make(from, to, Move::PROMOTE_TO_QUEEN_FLAG));
        ml.push_back(Move::make(from, to, Move::PROMOTE_TO_KNIGHT_FLAG));
        ml.push_back(Move::make(from, to, Move::PROMOTE_TO_ROOK_FLAG));
        ml.push_back(Move::make(from, to, Move::PROMOTE_TO_BISHOP_FLAG));
    }

    for (Square to : Bitloop(single & ~PROMO_RANK & target_mask))
    {
        ml.push_back(Move::make(pawn_from_up<US>(to), to));
    }

    const u64 dp = shift_pawn_up<US>(single & RANK_3_POV) & empty & target_mask;
    for (Square to : Bitloop(dp))
    {
        ml.push_back(Move::make(pawn_from_up_2<US>(to), to, Move::PAWN_TWO_UP_FLAG));
    }

    const u64 cap_west = shift_pawn_west<US>(free_pawns) & their & target_mask;
    for (Square to : Bitloop(cap_west))
    {
        Square from = pawn_from_west<US>(to);
        if ((1ULL << to) & PROMO_RANK)
        {
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_QUEEN_FLAG));
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_KNIGHT_FLAG));
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_ROOK_FLAG));
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_BISHOP_FLAG));
        }
        else
        {
            ml.push_back(Move::make(from, to));
        }
    }

    const u64 cap_east = shift_pawn_east<US>(free_pawns) & their & target_mask;
    for (Square to : Bitloop(cap_east))
    {
        Square from = pawn_from_east<US>(to);
        if ((1ULL << to) & PROMO_RANK)
        {
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_QUEEN_FLAG));
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_KNIGHT_FLAG));
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_ROOK_FLAG));
            ml.push_back(Move::make(from, to, Move::PROMOTE_TO_BISHOP_FLAG));
        }
        else
        {
            ml.push_back(Move::make(from, to));
        }
    }

    for (Square from : Bitloop(pinned_pawns))
    {
        const u64 pin_ray = line_through(ksq, from);

        Square one = (US == Color::WHITE) ? from + 8 : from - 8;
        if ((empty & (1ULL << one)) && (pin_ray & (1ULL << one)) && (target_mask & (1ULL << one)))
        {
            if ((1ULL << one) & PROMO_RANK)
            {
                ml.push_back(Move::make(from, one, Move::PROMOTE_TO_QUEEN_FLAG));
                ml.push_back(Move::make(from, one, Move::PROMOTE_TO_KNIGHT_FLAG));
                ml.push_back(Move::make(from, one, Move::PROMOTE_TO_ROOK_FLAG));
                ml.push_back(Move::make(from, one, Move::PROMOTE_TO_BISHOP_FLAG));
            }
            else
            {
                ml.push_back(Move::make(from, one));

                bool on_start = (US == Color::WHITE) ? (from / 8 == 1) : (from / 8 == 6);
                if (on_start)
                {
                    Square two = (US == Color::WHITE) ? from + 16 : from - 16;
                    if ((empty & (1ULL << two)) && (pin_ray & (1ULL << two)) && (target_mask & (1ULL << two)))
                    {
                        ml.push_back(Move::make(from, two, Move::PAWN_TWO_UP_FLAG));
                    }
                }
            }
        }

        u64 caps = pawn_attacks(from, US) & their & pin_ray & target_mask;
        for (Square to : Bitloop(caps))
        {
            if ((1ULL << to) & PROMO_RANK)
            {
                ml.push_back(Move::make(from, to, Move::PROMOTE_TO_QUEEN_FLAG));
                ml.push_back(Move::make(from, to, Move::PROMOTE_TO_KNIGHT_FLAG));
                ml.push_back(Move::make(from, to, Move::PROMOTE_TO_ROOK_FLAG));
                ml.push_back(Move::make(from, to, Move::PROMOTE_TO_BISHOP_FLAG));
            }
            else
            {
                ml.push_back(Move::make(from, to));
            }
        }
    }

    Square ep_sq = board.history[board.ply].ep_square;
    if (ep_sq != NO_SQUARE)
    {
        const u64 ep_mask = 1ULL << ep_sq;
        const Square captured_sq = (US == Color::WHITE) ? ep_sq - 8 : ep_sq + 8;
        const u64 captured_mask = 1ULL << captured_sq;

        u64 ep_attackers = pawn_attacks(ep_sq, THEM) & pawns;
        for (Square from : Bitloop(ep_attackers))
        {
            bool can_ep = true;

            if (pinned & (1ULL << from))
            {
                can_ep = (line_through(ksq, from) & ep_mask) != 0;
            }

            if (can_ep && (ksq / 8 == from / 8))
            {
                u64 new_occ = (occ ^ (1ULL << from) ^ captured_mask) | ep_mask;
                if (rook_attacks(ksq, new_occ) & board.ortho_sliders[them_idx])
                {
                    can_ep = false;
                }
            }

            if (can_ep && num_checkers == 1)
            {
                can_ep = (target_mask & ep_mask) != 0 || (checkers & captured_mask) != 0;
            }

            if (can_ep)
            {
                ml.push_back(Move::make(from, ep_sq, Move::ENPASSANT_CAPTURE_FLAG));
            }
        }
    }

    for (Square from : Bitloop(board.piece_bb[bb_index(Type::KNIGHT, us_idx)] & not_pinned))
    {
        add_moves(knight_attacks(from) & ~our & target_mask, from, ml);
    }

    const u64 bishops = board.piece_bb[bb_index(Type::BISHOP, us_idx)];
    for (Square from : Bitloop(bishops & not_pinned))
    {
        add_moves(bishop_attacks(from, occ) & ~our & target_mask, from, ml);
    }
    for (Square from : Bitloop(bishops & pinned))
    {
        add_moves(bishop_attacks(from, occ) & ~our & target_mask & line_through(ksq, from), from, ml);
    }

    const u64 rooks = board.piece_bb[bb_index(Type::ROOK, us_idx)];
    for (Square from : Bitloop(rooks & not_pinned))
    {
        add_moves(rook_attacks(from, occ) & ~our & target_mask, from, ml);
    }
    for (Square from : Bitloop(rooks & pinned))
    {
        add_moves(rook_attacks(from, occ) & ~our & target_mask & line_through(ksq, from), from, ml);
    }

    const u64 queens = board.piece_bb[bb_index(Type::QUEEN, us_idx)];
    for (Square from : Bitloop(queens & not_pinned))
    {
        add_moves(queen_attacks(from, occ) & ~our & target_mask, from, ml);
    }
    for (Square from : Bitloop(queens & pinned))
    {
        add_moves(queen_attacks(from, occ) & ~our & target_mask & line_through(ksq, from), from, ml);
    }
}
} // namespace

void generate_moves(const Board& board, MoveList& ml) noexcept
{
    if (board.side_to_move == Color::WHITE)
    {
        generate_moves_impl<Color::WHITE>(board, ml);
    }
    else
    {
        generate_moves_impl<Color::BLACK>(board, ml);
    }
}