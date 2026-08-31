#include "search.hpp"
#include "defines.hpp"
#include "magic.hpp"

#include <algorithm>
#include <cmath>

// clang-format off
constexpr i32 MVV_LVA[7][7] = {{0, 0, 0, 0, 0, 0, 0},
{                               0, 15, 14, 13, 12, 11, 10}, // Victim PAWN
                               {0, 25, 24, 23, 22, 21, 20}, // Victim KNIGHT
                               {0, 35, 34, 33, 32, 31, 30}, // Victim BISHOP
                               {0, 45, 44, 43, 42, 41, 40}, // Victim ROOK
                               {0, 55, 54, 53, 52, 51, 50}, // Victim QUEEN
                               {0, 0, 0, 0, 0, 0, 0}};
// clang-format on

static i32 LMR_TABLE[64][64];
static bool LMR_INIT = []() {
    for (i32 d = 1; d < 64; d++)
    {
        for (i32 m = 1; m < 64; m++)
        {
            LMR_TABLE[d][m] = static_cast<i32>(Params::lmr_base + std::log(d) * std::log(m) / Params::lmr_divisor);
            if (LMR_TABLE[d][m] < 0) LMR_TABLE[d][m] = 0;
        }
    }
    return true;
}();

constexpr std::array<Score, 7> SEE_VALUES = {0, 100, 300, 320, 500, 920, 20000};

constexpr Score see_piece_value(Type t)
{
    return SEE_VALUES[static_cast<usize>(t)];
}

static Type least_valuable_attacker(const Board& board, Bitboard attackers, Color side, Bitboard occ, Bitboard& from_bb)
{
    for (usize pt = 1; pt <= 6; pt++)
    {
        Type t = static_cast<Type>(pt);
        Bitboard candidates = attackers & board.piece_bb[bb_index(t, side)] & occ;
        if (candidates)
        {
            from_bb = candidates & (~candidates + 1);
            return t;
        }
    }
    return Type::EMPTY;
}

bool see_ge(const Board& board, Move move, Score threshold)
{
    Square from = move.get_start_square();
    Square to = move.get_target_square();
    u16 flag = move.get_flag();

    Piece attacker_piece = board.pieces[from];
    Type attacker_type = get_piece_type(attacker_piece);
    Color side = get_piece_color(attacker_piece);

    // Determine initial captured value.
    Score captured_value;
    if (flag == Move::ENPASSANT_CAPTURE_FLAG)
        captured_value = see_piece_value(Type::PAWN);
    else if (board.pieces[to] != EMPTY)
        captured_value = see_piece_value(get_piece_type(board.pieces[to]));
    else
        captured_value = 0;

    // Promotion changes attacker type.
    if (move.is_promotion())
    {
        Type promo_type = move.get_promotion_type();
        captured_value += see_piece_value(promo_type) - see_piece_value(Type::PAWN);
        attacker_type = promo_type;
    }

    // Initial balance.
    Score balance = captured_value - threshold;
    if (balance < 0) return false;

    balance -= see_piece_value(attacker_type);
    if (balance >= 0) return true;

    Bitboard occ = board.occupancy ^ (1ULL << from);
    if (flag == Move::ENPASSANT_CAPTURE_FLAG)
    {
        Square ep_sq = (side == Color::WHITE) ? to - 8 : to + 8;
        occ ^= (1ULL << ep_sq);
    }

    Bitboard attackers = board.attackers_to(to, occ);
    Color stm = !side;

    while (true)
    {
        attackers &= occ;
        Bitboard stm_attackers = attackers & board.color_bb[color_index(stm)];
        if (!stm_attackers) break;

        Bitboard from_bb;
        Type pt = least_valuable_attacker(board, attackers, stm, occ, from_bb);
        if (pt == Type::EMPTY) break;

        occ ^= from_bb;
        // Discover new sliders behind the removed piece.
        attackers |=
            (bishop_attacks(to, occ) & (board.pieces_of_type(Type::BISHOP) | board.pieces_of_type(Type::QUEEN)));
        attackers |= (rook_attacks(to, occ) & (board.pieces_of_type(Type::ROOK) | board.pieces_of_type(Type::QUEEN)));

        stm = !stm;
        balance = -balance - 1 - see_piece_value(pt);

        if (balance >= 0) break;
    }

    return (stm != side);
}

inline void pick_best(MoveList& ml, usize start)
{
    usize best = start;
    for (usize i = start + 1; i < ml.size(); ++i)
        if (ml[i].score > ml[best].score) best = i;
    if (best != start) std::swap(ml[start], ml[best]);
}

inline void
score_moves(const Board& board, MoveList& ml, Move tt_move, const SearchState& state, i32 ply, Move prev_move)
{
    Piece prev_piece = EMPTY;
    Square prev_to = 0;
    if (prev_move.is_some())
    {
        prev_piece = board.pieces[prev_move.get_target_square()];
        prev_to = prev_move.get_target_square();
    }

    usize us = color_index(board.side_to_move);

    for (ScoredMove& sm : ml)
    {
        Move m = sm.move;
        if (m == tt_move)
        {
            sm.score = Params::tt_move_score;
            continue;
        }

        if (m.is_capture())
        {
            Square to = m.get_target_square();
            Square from = m.get_start_square();
            Piece victim = board.pieces[to];
            Piece attacker = board.pieces[from];

            Type v_type = (victim == EMPTY) ? Type::PAWN : get_piece_type(victim);
            Type a_type = get_piece_type(attacker);

            sm.score = MVV_LVA[static_cast<usize>(v_type)][static_cast<usize>(a_type)];

            if (see_ge(board, m, 0))
                sm.score += Params::good_capture;
            else
                sm.score += Params::bad_capture;
        }
        else if (m.is_promotion())
        {
            sm.score = Params::promotion_bonus + static_cast<Score>(m.get_promotion_type());
        }
        else
        {
            // Quiet move ordering.
            if (m == state.killers[ply][0])
            {
                sm.score = Params::killer_score_0;
            }
            else if (m == state.killers[ply][1])
            {
                sm.score = Params::killer_score_1;
            }
            else if (prev_move.is_some() && m == state.countermoves[prev_piece][prev_to])
            {
                sm.score = Params::countermove_score;
            }
            else
            {
                Piece piece = board.pieces[m.get_start_square()];
                Square to = m.get_target_square();

                i32 score = state.history[us][m.from_to_index()];

                if (prev_move.is_some()) score += state.cont_history[0][prev_piece][prev_to][piece][to];

                if (ply >= 2 && board.ply >= 2)
                {
                    const State& gp_state = board.history[board.ply - 2];
                    if (gp_state.move.is_some())
                    {
                        Piece gp_piece = gp_state.moved_piece;
                        Square gp_to = gp_state.move.get_target_square();
                        score += state.cont_history[1][gp_piece][gp_to][piece][to];
                    }
                }

                sm.score = score;
            }
        }
    }
}

constexpr i32 HISTORY_MAX = 16384;

inline void update_history(i32& entry, i32 bonus)
{
    entry += bonus - entry * std::abs(bonus) / HISTORY_MAX;
}

inline void update_cont_history(i16& entry, i32 bonus)
{
    constexpr i32 CONT_MAX = 16384;
    i32 val = static_cast<i32>(entry);
    val += bonus - val * std::abs(bonus) / CONT_MAX;
    entry = static_cast<i16>(std::clamp(val, -CONT_MAX, CONT_MAX));
}

inline void update_quiet_stats(
    SearchState& state, const Board& board, Move best_move, i32 depth, i32 ply, Move prev_move, Move* searched_quiets,
    i32 quiet_count
)
{
    i32 bonus = std::min(depth * depth * Params::history_bonus_mult, Params::history_bonus_max);
    usize us = color_index(board.side_to_move);
    Piece best_piece = board.pieces[best_move.get_start_square()];
    Square best_to = best_move.get_target_square();

    Piece prev_piece = EMPTY;
    Square prev_to = 0;
    if (prev_move.is_some())
    {
        prev_piece = board.pieces[prev_move.get_target_square()];
        prev_to = prev_move.get_target_square();
    }

    // Update killer moves.
    if (!(best_move == state.killers[ply][0]))
    {
        state.killers[ply][1] = state.killers[ply][0];
        state.killers[ply][0] = best_move;
    }

    // History bonus for the move that caused cutoff.
    update_history(state.history[us][best_move.from_to_index()], bonus);

    // Countermove.
    if (prev_move.is_some())
    {
        state.countermoves[prev_piece][prev_to] = best_move;

        // Continuation history bonus (1-ply).
        update_cont_history(state.cont_history[0][prev_piece][prev_to][best_piece][best_to], bonus);
    }

    // Continuation history bonus (2-ply).
    if (ply >= 2 && board.ply >= 2)
    {
        const State& gp_state = board.history[board.ply - 2];
        if (gp_state.move.is_some())
        {
            Piece gp_piece = gp_state.moved_piece;
            Square gp_to = gp_state.move.get_target_square();
            update_cont_history(state.cont_history[1][gp_piece][gp_to][best_piece][best_to], bonus);
        }
    }

    // History malus for quiet moves that did not cause cutoff.
    for (i32 i = 0; i < quiet_count; i++)
    {
        Move m = searched_quiets[i];
        if (m == best_move) continue;

        update_history(state.history[us][m.from_to_index()], -bonus);

        Piece piece = board.pieces[m.get_start_square()];
        Square to = m.get_target_square();

        if (prev_move.is_some()) update_cont_history(state.cont_history[0][prev_piece][prev_to][piece][to], -bonus);

        if (ply >= 2 && board.ply >= 2)
        {
            const State& gp_state = board.history[board.ply - 2];
            if (gp_state.move.is_some())
            {
                Piece gp_piece = gp_state.moved_piece;
                Square gp_to = gp_state.move.get_target_square();
                update_cont_history(state.cont_history[1][gp_piece][gp_to][piece][to], -bonus);
            }
        }
    }
}

Score qsearch(Board& board, Score alpha, Score beta, SearchState& state)
{
    if (state.time_up()) return 0;

    Score stand_pat = board.evaluate();

    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    MoveList ml = board.generate_moves<GenType::CAPTURES>();
    score_moves(board, ml, Move {}, state, 0, Move {});

    for (usize idx = 0; idx < ml.size(); ++idx)
    {
        pick_best(ml, idx);
        if (ml[idx].score < Params::good_capture) break;

        Move m = ml[idx].move;

        // Delta Pruning.
        Score captured_value = 0;
        if (m.get_flag() == Move::ENPASSANT_CAPTURE_FLAG)
        {
            captured_value = see_piece_value(Type::PAWN);
        }
        else
        {
            Piece victim = board.pieces[m.get_target_square()];
            if (victim != EMPTY) captured_value = see_piece_value(get_piece_type(victim));
        }

        if (m.is_promotion()) captured_value += see_piece_value(m.get_promotion_type()) - see_piece_value(Type::PAWN);

        if (stand_pat + captured_value + Params::delta_margin < alpha) continue;

        board.make_move(m);
        state.nodes++;
        Score score = -qsearch(board, -beta, -alpha, state);
        board.unmake_move(ml[idx].move);

        if (state.stop && state.stop->load(std::memory_order_relaxed)) return 0;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

Score negamax(Board& board, i32 depth, i32 ply, Score alpha, Score beta, SearchState& state)
{
    if (state.time_up()) return 0;

    state.nodes++;

    if (ply > 0 && board.is_draw()) return 0;

    if (ply >= static_cast<i32>(MAX_PLY) - 1) return board.evaluate();

    u64 hash = board.zobrist_key();

    std::optional<TTEntry> tt_entry = state.tt->probe(hash, ply);
    if (tt_entry)
    {
        if (ply > 0 && tt_entry->depth >= depth)
        {
            Bound b = tt_entry->get_bound();
            if (b == Bound::EXACT) return tt_entry->score;
            if (b == Bound::UPPER && tt_entry->score <= alpha) return tt_entry->score;
            if (b == Bound::LOWER && tt_entry->score >= beta) return tt_entry->score;
        }
    }

    if (depth <= 0) return qsearch(board, alpha, beta, state);

    bool in_check = board.in_check();
    Score static_eval = in_check ? 0 : board.evaluate();

    // Internal Iterative Reduction.
    if (depth >= Params::iir_min_depth && (!tt_entry || tt_entry->move.is_null())) depth -= Params::iir_reduction;

    if (!in_check && ply > 0)
    {
        // Reverse Futility Pruning.
        if (depth <= Params::rfp_max_depth && static_eval - depth * Params::rfp_multiplier >= beta) return static_eval;

        // Null Move Pruning.
        if (depth >= Params::nmp_min_depth)
        {
            bool prev_was_null = board.history[board.ply - 1].move.is_null();
            if (!prev_was_null && board.has_non_pawn_material(board.side_to_move))
            {
                if (static_eval >= beta)
                {
                    i32 R = Params::nmp_base_r + depth / Params::nmp_depth_divisor;
                    board.make_null();
                    Score null_score = -negamax(board, depth - 1 - R, ply + 1, -beta, -beta + 1, state);
                    board.unmake_null();

                    if (state.stop && state.stop->load(std::memory_order_relaxed)) return 0;

                    if (null_score >= beta) return null_score >= MATE_THRESHOLD ? beta : null_score;
                }
            }
        }
    }

    Move prev_move = (ply > 0) ? board.history[board.ply - 1].move : Move {};

    Move tt_move = tt_entry ? tt_entry->move : Move {};

    MoveList ml = board.generate_moves<GenType::ALL>();
    score_moves(board, ml, tt_move, state, ply, prev_move);

    Move best_move {};
    Score best_score = -INF;
    Bound bound = Bound::UPPER;

    i32 moves_played = 0;

    bool do_futility_pruning =
        !in_check && depth <= Params::fp_max_depth && static_eval + depth * Params::fp_multiplier <= alpha;

    Move searched_quiets[64];
    i32 quiet_count = 0;

    for (usize idx = 0; idx < ml.size(); ++idx)
    {
        pick_best(ml, idx);
        Move m = ml[idx].move;

        bool is_quiet = !m.is_capture() && !m.is_promotion();

        // Futility Pruning.
        if (do_futility_pruning && is_quiet && moves_played > 0 && best_score > -MATE_THRESHOLD) continue;

        board.make_move(m);

        // Check Extension.
        i32 extension = board.in_check() ? 1 : 0;
        i32 new_depth = depth - 1 + extension;

        Score score;

        if (moves_played == 0)
        {
            // Full-depth full-window for first move.
            score = -negamax(board, new_depth, ply + 1, -beta, -alpha, state);
        }
        else
        {
            // Late Move Reductions.
            if (depth >= 3 && moves_played >= 3 && is_quiet && !in_check)
            {
                i32 R = LMR_TABLE[std::min(depth, 63)][std::min(moves_played, 63)];

                // Reduce less for killers and countermoves.
                if (m == state.killers[ply][0] || m == state.killers[ply][1]) R -= 1;
                if (prev_move.is_some())
                {
                    Piece pm_piece = board.pieces[prev_move.get_target_square()];
                    Square pm_to = prev_move.get_target_square();
                    if (m == state.countermoves[pm_piece][pm_to]) R -= 1;
                }

                // Reduce less/more based on history score.
                R -= state.history[color_index(!board.side_to_move)][m.from_to_index()] / Params::lmr_history_divisor;

                R = std::clamp(R, 0, new_depth - 1);

                score = -negamax(board, new_depth - R, ply + 1, -alpha - 1, -alpha, state);
            }
            else
            {
                // Force a full-depth zero-window search.
                score = alpha + 1;
            }

            // Full-depth zero-window search.
            if (score > alpha) score = -negamax(board, new_depth, ply + 1, -alpha - 1, -alpha, state);

            // Full-depth full-window re-search (only if score is inside window).
            if (score > alpha && score < beta) score = -negamax(board, new_depth, ply + 1, -beta, -alpha, state);
        }

        board.unmake_move(m);

        if (state.stop && state.stop->load(std::memory_order_relaxed)) return 0;

        if (is_quiet && quiet_count < 64) searched_quiets[quiet_count++] = m;

        moves_played++;

        if (score > best_score)
        {
            best_score = score;
            best_move = m;
        }

        if (score > alpha)
        {
            alpha = score;
            bound = Bound::EXACT;
        }

        if (alpha >= beta)
        {
            bound = Bound::LOWER;

            if (is_quiet) update_quiet_stats(state, board, m, depth, ply, prev_move, searched_quiets, quiet_count);

            break;
        }
    }

    if (moves_played == 0)
    {
        if (board.in_check())
            return -MATE_VALUE + ply;
        else
            return 0;
    }

    state.tt->store(hash, depth, ply, best_score, bound, best_move);

    return best_score;
}

RootResult search_root(Board& board, i32 depth, Score alpha, Score beta, SearchState& state)
{
    RootResult result;
    result.score = -INF;
    result.completed = false;

    u64 hash = board.zobrist_key();
    std::optional<TTEntry> tt_entry = state.tt->probe(hash, 0);
    Move tt_move = tt_entry ? tt_entry->move : Move {};

    MoveList ml = board.generate_moves<GenType::ALL>();
    score_moves(board, ml, tt_move, state, 0, Move {});

    Score best_score = -INF;
    Move best_move {};
    Bound bound = Bound::UPPER;
    i32 moves_played = 0;

    for (usize idx = 0; idx < ml.size(); ++idx)
    {
        pick_best(ml, idx);
        Move m = ml[idx].move;
        board.make_move(m);

        Score score;

        if (moves_played == 0)
        {
            score = -negamax(board, depth - 1, 1, -beta, -alpha, state);
        }
        else
        {
            // PVS (zero-window search first).
            score = -negamax(board, depth - 1, 1, -alpha - 1, -alpha, state);

            // Re-search with full window if score is inside the window.
            if (score > alpha && score < beta) score = -negamax(board, depth - 1, 1, -beta, -alpha, state);
        }

        board.unmake_move(m);

        if (state.stop && state.stop->load(std::memory_order_relaxed)) return result;

        moves_played++;

        if (score > best_score)
        {
            best_score = score;
            best_move = m;
        }

        if (score > alpha)
        {
            alpha = score;
            bound = Bound::EXACT;
        }

        if (alpha >= beta)
        {
            bound = Bound::LOWER;
            break;
        }
    }

    state.tt->store(hash, depth, 0, best_score, bound, best_move);

    result.score = best_score;
    result.best_move = best_move;
    result.completed = true;

    return result;
}
