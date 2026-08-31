#include "search.hpp"
#include "defines.hpp"

inline void pick_best(MoveList& ml, usize start)
{
    usize best = start;
    for (usize i = start + 1; i < ml.size(); ++i)
        if (ml[i].score > ml[best].score) best = i;
    if (best != start) std::swap(ml[start], ml[best]);
}

constexpr Score CAPTURE_BONUS = 10000;
constexpr Score PROMOTION_BONUS = 20000;
constexpr Score TT_MOVE_SCORE = 30000;

#include <cmath>

static i32 LMR_TABLE[64][64];
static bool LMR_INIT = []() {
    for (i32 d = 1; d < 64; d++)
    {
        for (i32 m = 1; m < 64; m++)
        {
            LMR_TABLE[d][m] = static_cast<i32>(0.75 + std::log(d) * std::log(m) / 2.25);
            if (LMR_TABLE[d][m] < 0) LMR_TABLE[d][m] = 0;
        }
    }
    return true;
}();

// clang-format off
constexpr i32 MVV_LVA[7][7] = {{0, 0, 0, 0, 0, 0, 0},       
{                               0, 15, 14, 13, 12, 11, 10}, // Victim PAWN
                               {0, 25, 24, 23, 22, 21, 20}, // Victim KNIGHT
                               {0, 35, 34, 33, 32, 31, 30}, // Victim BISHOP
                               {0, 45, 44, 43, 42, 41, 40}, // Victim ROOK
                               {0, 55, 54, 53, 52, 51, 50}, // Victim QUEEN
                               {0, 0, 0, 0, 0, 0, 0}};
// clang-format on

constexpr std::array<Score, 6> PIECE_VALUES {100, 300, 320, 500, 920, 0};

inline bool see_capture(const Board& board, Move move)
{
    Square from = move.get_start_square();
    Square to = move.get_target_square();
    Piece captured = board.pieces[to];
    Piece attacker = board.pieces[from];

    if (captured == EMPTY) return true; // En-passant

    Type v_type = get_piece_type(captured);
    Type a_type = get_piece_type(attacker);

    if (v_type >= a_type) return true;

    Score gain = PIECE_VALUES[static_cast<usize>(v_type) - 1];
    gain -= PIECE_VALUES[static_cast<usize>(a_type) - 1];

    if (gain >= 0) return true;

    Bitboard occ = board.occupancy ^ (1ULL << from);
    Color opponent = !board.side_to_move;

    Bitboard attackers = board.attackers_to(to, occ);
    Bitboard enemy_attackers = attackers & board.color_bb[static_cast<usize>(opponent)];

    if (enemy_attackers == 0) return true;

    return false;
}

inline void score_moves(const Board& board, MoveList& ml, Move tt_move)
{
    for (ScoredMove& sm : ml)
    {
        Move m = sm.move;
        if (m == tt_move)
        {
            sm.score = TT_MOVE_SCORE;
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

            sm.score = CAPTURE_BONUS + MVV_LVA[static_cast<usize>(v_type)][static_cast<usize>(a_type)];

            if (!see_capture(board, m)) sm.score -= 5000;
        }
        else if (m.is_promotion())
            sm.score = PROMOTION_BONUS + static_cast<Score>(m.get_promotion_type());
        else
            sm.score = 0;
    }
}

Score qsearch(Board& board, Score alpha, Score beta, SearchState& state)
{
    if (state.time_up()) return 0;

    Score stand_pat = board.evaluate();

    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    MoveList ml = board.generate_moves<GenType::CAPTURES>();
    score_moves(board, ml, Move {});

    for (usize idx = 0; idx < ml.size(); ++idx)
    {
        pick_best(ml, idx);
        if (ml[idx].score < CAPTURE_BONUS) break;

        board.make_move(ml[idx].move);
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

    if (!in_check && ply > 0)
    {
        // Reverse Futility Pruning.
        if (depth <= 5 && static_eval - depth * 75 >= beta) return static_eval;

        // Null Move Pruning.
        if (depth >= 3)
        {
            bool prev_was_null = board.history[board.ply - 1].move.is_null();
            if (!prev_was_null && board.has_non_pawn_material(board.side_to_move))
            {
                if (static_eval >= beta)
                {
                    i32 R = 3 + depth / 6;
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

    MoveList ml = board.generate_moves<GenType::ALL>();
    score_moves(board, ml, tt_move);

    Move best_move {};
    Score best_score = -INF;
    Bound bound = Bound::UPPER;

    int moves_played = 0;

    bool do_futility_pruning = !in_check && depth <= 4 && static_eval + depth * 150 <= alpha;

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

                // Reduced-depth zero-window search
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
    score_moves(board, ml, tt_move);

    Score best_score = -INF;
    Move best_move {};
    Bound bound = Bound::UPPER;
    int moves_played = 0;

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
