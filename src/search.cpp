#include "search.hpp"
#include "board.hpp"
#include "defines.hpp"
#include "magic.hpp"
#include "zobrist.hpp"

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

void init_lmr_table_internal()
{
    for (i32 d = 1; d < 64; d++)
    {
        for (i32 m = 1; m < 64; m++)
        {
            f64 lmr_base = Params::lmr_base_100 / 100.0;
            f64 lmr_divisor = Params::lmr_divisor_100 / 100.0;
            LMR_TABLE[d][m] = static_cast<i32>(lmr_base + std::log(d) * std::log(m) / lmr_divisor);
            if (LMR_TABLE[d][m] < 0) LMR_TABLE[d][m] = 0;
        }
    }
}

static bool LMR_INIT = []() {
    init_lmr_table_internal();
    return true;
}();

#ifdef TUNE_BUILD
namespace Params
{
void init_lmr_table()
{
    init_lmr_table_internal();
}
} // namespace Params
#endif

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
        }
        else if (m.is_capture())
        {
            Square to = m.get_target_square();
            Square from = m.get_start_square();
            Piece victim = board.pieces[to];
            Piece attacker = board.pieces[from];

            Type v_type = (victim == EMPTY) ? Type::PAWN : get_piece_type(victim);
            Type a_type = get_piece_type(attacker);

            sm.score = MVV_LVA[static_cast<usize>(v_type)][static_cast<usize>(a_type)];

            // Capture history bonus.
            sm.score += state.capture_history[attacker][to][static_cast<usize>(v_type)];

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
                    const State& gp_state = (*board.history)[board.ply - 2];
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

inline void update_capture_stats(
    SearchState& state, const Board& board, Move best_move, i32 depth, Move* searched_captures, i32 capture_count
)
{
    i32 bonus = std::min(depth * depth * Params::capture_history_bonus_mult, Params::capture_history_bonus_max);

    // Malus for captures that didn't cause cutoff.
    for (i32 i = 0; i < capture_count; i++)
    {
        Move m = searched_captures[i];

        Square to = m.get_target_square();
        Piece attacker = board.pieces[m.get_start_square()];
        Piece victim = board.pieces[to];
        Type v_type = (victim == EMPTY) ? Type::PAWN : get_piece_type(victim);

        i16& entry = state.capture_history[attacker][to][static_cast<usize>(v_type)];

        if (m == best_move)
            update_cont_history(entry, bonus);
        else
            update_cont_history(entry, -bonus);
    }
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
        const State& gp_state = (*board.history)[board.ply - 2];
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
            const State& gp_state = (*board.history)[board.ply - 2];
            if (gp_state.move.is_some())
            {
                Piece gp_piece = gp_state.moved_piece;
                Square gp_to = gp_state.move.get_target_square();
                update_cont_history(state.cont_history[1][gp_piece][gp_to][piece][to], -bonus);
            }
        }
    }
}

inline Score get_static_eval(const Board& board, const SearchState& state)
{
    Score eval = board.evaluate();

    u64 pawn_hash = zobrist::compute_pawn_hash(board);
    i16 ch = state.correction_history[color_index(board.side_to_move)][pawn_hash & 16383];
    eval = std::clamp<Score>(eval + ch, -MATE_VALUE, MATE_VALUE);
    return eval;
}

Score qsearch(Board& board, Score alpha, Score beta, SearchState& state, i32 ply, Move prev_move = Move {})
{
    if (state.time_up()) return 0;

    if (ply > 0 && board.is_draw(ply)) return 0;

    if (ply >= static_cast<i32>(MAX_PLY) - 1) return board.evaluate();

    // Mate distance pruning.
    alpha = std::max(alpha, -MATE_VALUE + ply);
    beta = std::min(beta, MATE_VALUE - ply - 1);
    if (alpha >= beta) return alpha;

    u64 hash = board.zobrist_key();
    std::optional<TTEntry> tt_entry = state.tt->probe(hash, ply);
    Move tt_move = Move {};

    if (tt_entry)
    {
        tt_move = tt_entry->move;
        Bound b = tt_entry->get_bound();
        if (b == Bound::EXACT) return tt_entry->score;
        if (b == Bound::UPPER && tt_entry->score <= alpha) return tt_entry->score;
        if (b == Bound::LOWER && tt_entry->score >= beta) return tt_entry->score;
    }

    bool in_check = board.in_check();
    Score stand_pat = -INF;
    Score best_score = -INF;

    if (!in_check)
    {
        stand_pat = get_static_eval(board, state);
        best_score = stand_pat;

        if (stand_pat >= beta)
        {
            state.tt->store(hash, 0, ply, stand_pat, Bound::LOWER, Move {});
            return stand_pat;
        }
        if (alpha < stand_pat) alpha = stand_pat;
    }

    MoveList ml = in_check ? board.generate_moves<GenType::ALL>() : board.generate_moves<GenType::CAPTURES>();

    if (in_check && ml.empty()) return -MATE_VALUE + ply;

    score_moves(board, ml, tt_move, state, ply, prev_move);

    Score original_alpha = alpha;
    Move best_move = Move {};

    for (usize idx = 0; idx < ml.size(); ++idx)
    {
        pick_best(ml, idx);
        if (!in_check && ml[idx].score < 0) break;

        Move m = ml[idx].move;

        // Delta Pruning.
        if (!in_check)
        {
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

            if (m.is_promotion())
                captured_value += see_piece_value(m.get_promotion_type()) - see_piece_value(Type::PAWN);

            if (stand_pat + captured_value + Params::delta_margin < alpha) continue;
        }

        board.make_move(m);
        state.nodes++;
        Score score = -qsearch(board, -beta, -alpha, state, ply + 1, m);
        board.unmake_move(ml[idx].move);

        if (state.stop && state.stop->load(std::memory_order_relaxed)) return 0;

        if (score > best_score)
        {
            best_score = score;
            best_move = m;
        }

        if (score >= beta)
        {
            state.tt->store(hash, 0, ply, best_score, Bound::LOWER, best_move);
            return best_score;
        }

        if (score > alpha) alpha = score;
    }

    Bound b = (best_score > original_alpha) ? (in_check ? Bound::EXACT : Bound::UPPER) : Bound::UPPER;
    state.tt->store(hash, 0, ply, best_score, b, best_move);

    return best_score;
}

Score negamax(
    Board& board, i32 depth, i32 ply, Score alpha, Score beta, SearchState& state, Move excluded_move = Move {},
    i32 double_ext = 0
)
{
    if (state.time_up()) return 0;

    state.nodes++;

    if (ply > 0 && board.is_draw(ply)) return 0;

    if (ply >= static_cast<i32>(MAX_PLY) - 1) return board.evaluate();

    // Mate distance pruning.
    if (ply > 0)
    {
        alpha = std::max(alpha, -MATE_VALUE + ply);
        beta = std::min(beta, MATE_VALUE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    u64 hash = board.zobrist_key();

    std::optional<TTEntry> tt_entry = state.tt->probe(hash, ply);
    if (tt_entry && !excluded_move.is_some())
    {
        if (ply > 0 && tt_entry->depth >= depth)
        {
            Bound b = tt_entry->get_bound();
            if (b == Bound::EXACT) return tt_entry->score;
            if (b == Bound::UPPER && tt_entry->score <= alpha) return tt_entry->score;
            if (b == Bound::LOWER && tt_entry->score >= beta) return tt_entry->score;
        }
    }

    if (depth <= 0) return qsearch(board, alpha, beta, state, ply);

    Move prev_move = board.ply > 0 ? (*board.history)[board.ply - 1].move : Move {};
    bool in_check = board.in_check();
    Score static_eval = in_check ? 0 : get_static_eval(board, state);
    state.evals[ply] = static_eval;

    i16* ch_entry = nullptr;
    if (!in_check)
    {
        u64 pawn_hash = zobrist::compute_pawn_hash(board);
        ch_entry = &state.correction_history[color_index(board.side_to_move)][pawn_hash % 16384];
    }

    bool improving = false;
    if (ply >= 2 && !in_check) improving = (static_eval >= state.evals[ply - 2]);

    // Internal Iterative Reduction.
    if (!excluded_move.is_some() && depth >= Params::iir_min_depth && (!tt_entry || tt_entry->move.is_null()))
        depth -= Params::iir_reduction;

    if (!in_check && ply > 0 && !excluded_move.is_some())
    {
        // Razoring.
        if (depth <= Params::razoring_max_depth && static_eval + Params::razoring_margin * depth <= alpha)
        {
            Score r_score = qsearch(board, alpha, beta, state, ply, prev_move);
            if (r_score <= alpha) return r_score;
        }

        // Reverse Futility Pruning.
        if (depth <= Params::rfp_max_depth && std::abs(beta) < MATE_THRESHOLD && std::abs(static_eval) < MATE_THRESHOLD)
        {
            Score rfp_margin = depth * Params::rfp_multiplier;
            if (improving) rfp_margin -= Params::rfp_improving_margin_bonus;
            if (static_eval - rfp_margin >= beta) return static_eval;
        }

        // ProbCut.
        if (depth >= Params::pc_min_depth && std::abs(beta) < MATE_THRESHOLD && static_eval + Params::pc_margin >= beta)
        {
            Score pc_beta = beta + Params::pc_margin;
            i32 pc_depth = depth - Params::pc_depth_reduction;

            bool skip_probcut = false;
            if (tt_entry && tt_entry->depth >= pc_depth && tt_entry->score < pc_beta &&
                tt_entry->get_bound() == Bound::UPPER)
                skip_probcut = true;

            if (!skip_probcut)
            {
                Move tt_m = tt_entry ? tt_entry->move : Move {};
                MoveList pc_moves = board.generate_moves<GenType::CAPTURES>();
                score_moves(board, pc_moves, tt_m, state, ply, prev_move);

                for (usize i = 0; i < pc_moves.size(); ++i)
                {
                    pick_best(pc_moves, i);
                    Move m = pc_moves[i].move;

                    if (!see_ge(board, m, 0)) continue;

                    board.make_move(m);

                    Score pc_score;
                    if (pc_depth - 1 <= 0)
                        pc_score = -qsearch(board, -pc_beta, -pc_beta + 1, state, ply + 1, m);
                    else
                        pc_score = -negamax(board, pc_depth - 1, ply + 1, -pc_beta, -pc_beta + 1, state);

                    board.unmake_move(m);

                    if (state.stop && state.stop->load(std::memory_order_relaxed)) return 0;

                    if (pc_score >= pc_beta)
                    {
                        state.tt->store(hash, pc_depth, ply, pc_beta, Bound::LOWER, m);
                        return pc_beta;
                    }
                }
            }
        }

        // Null Move Pruning.
        if (depth >= Params::nmp_min_depth)
        {
            bool prev_was_null = (*board.history)[board.ply - 1].move.is_null();
            if (!prev_was_null && board.has_non_pawn_material(board.side_to_move))
            {
                if (static_eval >= beta)
                {
                    i32 R = Params::nmp_base_r + depth / Params::nmp_depth_divisor;
                    R += std::min(Params::nmp_max_eval_r, (static_eval - beta) / Params::nmp_margin_divisor);
                    R = std::min(R, depth);

                    board.make_null();
                    Score null_score = -negamax(board, depth - 1 - R, ply + 1, -beta, -beta + 1, state);
                    board.unmake_null();

                    if (state.stop && state.stop->load(std::memory_order_relaxed)) return 0;

                    if (null_score >= beta) return null_score >= MATE_THRESHOLD ? beta : null_score;
                }
            }
        }
    }

    Move tt_move = tt_entry ? tt_entry->move : Move {};
    bool tt_is_singular = false;
    bool tt_was_tested = false;

    // Singular Extensions.
    if (!excluded_move.is_some() && depth >= Params::se_min_depth && tt_entry && tt_move.is_some() && ply > 0)
    {
        if (tt_entry->depth >= depth - Params::se_depth_reduction && tt_entry->get_bound() != Bound::UPPER)
        {
            Score tt_score = tt_entry->score;
            Score se_beta = std::max(-MATE_VALUE, tt_score - Params::se_margin);
            i32 se_depth = depth - Params::se_depth_reduction;

            tt_was_tested = true;
            Score se_score = negamax(board, se_depth, ply, se_beta - 1, se_beta, state, tt_move);
            if (se_score < se_beta) tt_is_singular = true;
        }
    }

    MoveList ml = board.generate_moves<GenType::ALL>();
    score_moves(board, ml, tt_move, state, ply, prev_move);

    Move best_move {};
    Score best_score = -INF;
    Bound bound = Bound::UPPER;

    i32 moves_played = 0;

    bool do_futility_pruning = false;
    if (!in_check && depth <= Params::fp_max_depth && std::abs(alpha) < MATE_THRESHOLD)
    {
        Score fp_margin = depth * Params::fp_multiplier;
        if (improving) fp_margin -= Params::fp_improving_margin_bonus;
        if (static_eval + fp_margin <= alpha) do_futility_pruning = true;
    }

    Move searched_quiets[64];
    i32 quiet_count = 0;

    Move searched_captures[64];
    i32 capture_count = 0;

    for (usize idx = 0; idx < ml.size(); ++idx)
    {
        pick_best(ml, idx);
        Move m = ml[idx].move;

        if (m == excluded_move) continue;

        bool is_quiet = !m.is_capture() && !m.is_promotion();
        bool is_killer = (m == state.killers[ply][0] || m == state.killers[ply][1]);
        bool is_countermove =
            prev_move.is_some() &&
            (m == state.countermoves[board.pieces[prev_move.get_target_square()]][prev_move.get_target_square()]);

        // SEE Pruning.
        if (best_score > -MATE_THRESHOLD && depth <= Params::see_pruning_max_depth && m != tt_move && !is_killer)
        {
            if (!is_quiet && !see_ge(board, m, Params::see_capture_margin * depth)) continue;
            if (is_quiet && !see_ge(board, m, Params::see_quiet_margin * depth)) continue;
        }

        // Late Move Pruning.
        if (!in_check && depth <= Params::lmp_max_depth && is_quiet && !is_killer)
        {
            i32 lmp_threshold =
                (Params::lmp_base + (depth * depth * Params::lmp_multiplier) / 10) / (improving ? 1 : 2);
            if (moves_played >= lmp_threshold) continue;
        }

        // History Pruning.
        if (!in_check && depth <= Params::hp_max_depth && is_quiet && !is_killer && !is_countermove &&
            moves_played > 0 && best_score > -MATE_THRESHOLD)
        {
            i32 hist = state.history[color_index(board.side_to_move)][m.from_to_index()];
            if (hist < -Params::hp_margin * depth) continue;
        }

        // Futility Pruning.
        if (do_futility_pruning && is_quiet && moves_played > 0 && best_score > -MATE_THRESHOLD) continue;

        board.make_move(m);

        // Check Extension.
        i32 extension = board.in_check() ? 1 : 0;
        i32 next_double_ext = double_ext;

        if (m == tt_move)
        {
            if (tt_is_singular)
            {
                if (extension == 0 && double_ext < Params::se_double_ext_cap)
                {
                    extension = Params::se_extension;
                    next_double_ext++;
                }
                else if (extension > 0)
                {
                    // Double extension if checking and singular.
                    if (double_ext < Params::se_double_ext_cap && std::abs(static_eval) < Params::se_double_ext_margin)
                    {
                        extension += 1;
                        next_double_ext++;
                    }
                }
            }
            else if (tt_was_tested && depth >= Params::se_min_depth && !excluded_move.is_some())
            {
                // Negative extension (TT move was tested but is not singular).
                extension -= Params::se_negative_extension_depth;
            }
        }

        i32 new_depth = depth - 1 + extension;

        Score score;

        if (moves_played == 0)
        {
            // Full-depth full-window for first move.
            score = -negamax(board, new_depth, ply + 1, -beta, -alpha, state, Move {}, next_double_ext);
        }
        else
        {
            // Late Move Reductions.
            if (depth >= 3 && moves_played >= 1 && !in_check && extension == 0)
            {
                i32 R = 0;
                if (is_quiet)
                {
                    R = LMR_TABLE[std::min(depth, 63)][std::min(moves_played, 63)];

                    if (improving) R -= Params::lmr_improving_reduction;

                    // Reduce less for killers and countermoves.
                    if (is_killer) R -= 1;
                    if (is_countermove) R -= 1;

                    // Reduce less/more based on history score.
                    R -= state.history[color_index(!board.side_to_move)][m.from_to_index()] /
                         Params::lmr_history_divisor;
                }
                else if (moves_played >= Params::lmr_capture_moves)
                {
                    // Capture LMR: reduce late or losing captures.
                    R = 1;
                    if (!see_ge(board, m, 0)) R += 1;
                    if (improving) R -= Params::lmr_improving_reduction;
                }

                if (R > 0)
                {
                    R = std::clamp(R, 1, new_depth - 1);
                    score =
                        -negamax(board, new_depth - R, ply + 1, -alpha - 1, -alpha, state, Move {}, next_double_ext);
                }
                else
                {
                    // Force a full-depth zero-window search.
                    score = alpha + 1;
                }
            }
            else
            {
                // Force a full-depth zero-window search.
                score = alpha + 1;
            }

            // Full-depth zero-window search.
            if (score > alpha)
                score = -negamax(board, new_depth, ply + 1, -alpha - 1, -alpha, state, Move {}, next_double_ext);

            // Full-depth full-window re-search (only if score is inside window).
            if (score > alpha && score < beta)
                score = -negamax(board, new_depth, ply + 1, -beta, -alpha, state, Move {}, next_double_ext);
        }

        board.unmake_move(m);

        if (state.stop && state.stop->load(std::memory_order_relaxed)) return 0;

        if (is_quiet && quiet_count < 64) searched_quiets[quiet_count++] = m;
        if (m.is_capture() && capture_count < 64) searched_captures[capture_count++] = m;

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

            if (is_quiet)
                update_quiet_stats(state, board, m, depth, ply, prev_move, searched_quiets, quiet_count);
            else if (m.is_capture())
                update_capture_stats(state, board, m, depth, searched_captures, capture_count);

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

    // Correction history update.
    if (ch_entry && depth >= 1 && !excluded_move.is_some() && std::abs(best_score) < MATE_THRESHOLD)
    {
        Score uncorrected = static_eval - *ch_entry;
        bool update = false;

        if (bound == Bound::EXACT)
            update = true;
        else if (bound == Bound::LOWER && best_score > uncorrected)
            update = true;
        else if (bound == Bound::UPPER && best_score < uncorrected)
            update = true;

        if (update)
        {
            Score diff = best_score - uncorrected;
            i32 weight = Params::ch_weight * 32;
            *ch_entry = static_cast<i16>(std::clamp<i32>(*ch_entry + diff / weight, -Params::ch_cap, Params::ch_cap));
        }
    }

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
    if (ml.empty())
    {
        result.score = board.in_check() ? -MATE_VALUE : 0;
        result.completed = true;
        return result;
    }

    score_moves(board, ml, tt_move, state, 0, Move {});

    Score best_score = -INF;
    Move best_move = tt_move.is_some() ? tt_move : ml[0].move;
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

        if (state.stop && state.stop->load(std::memory_order_relaxed))
        {
            if (moves_played > 0)
            {
                result.score = best_score;
                result.best_move = best_move;
            }
            return result;
        }

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
