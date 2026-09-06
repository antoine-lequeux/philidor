#pragma once

#include "board.hpp"
#include "defines.hpp"
#include "movelist.hpp"
#include "tt.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>

namespace Params
{
#ifdef TUNE_BUILD
    #include <string>
    #include <vector>

struct TunableParam
{
    std::string name;
    i32* ptr;
    i32 min, max;
};

inline std::vector<TunableParam>& tunable_registry()
{
    static std::vector<TunableParam> registry;
    return registry;
}

struct TunableRegistrar
{
    TunableRegistrar(const std::string& name, i32* ptr, i32 value, i32 min, i32 max)
    {
        *ptr = value;
        tunable_registry().push_back({name, ptr, min, max});
    }
};

    #define TUNABLE_PARAM(type, name, value, min, max)                                                                 \
        inline type name;                                                                                              \
        inline TunableRegistrar name##_registrar(#name, &name, value, min, max);
#else
    #define TUNABLE_PARAM(type, name, value, min, max) constexpr type name = value;
#endif

TUNABLE_PARAM(Score, tt_move_score, 2000000, 1500000, 2500000)
TUNABLE_PARAM(Score, promotion_bonus, 1500000, 1000000, 2000000)
TUNABLE_PARAM(Score, knight_promotion_score, 800000, 500000, 1200000)
TUNABLE_PARAM(Score, good_capture, 1000000, 750000, 1250000)
TUNABLE_PARAM(Score, killer_score_0, 600000, 450000, 750000)
TUNABLE_PARAM(Score, killer_score_1, 500000, 400000, 600000)
TUNABLE_PARAM(Score, countermove_score, 400000, 300000, 500000)
TUNABLE_PARAM(Score, bad_capture, -1000000, -1250000, -750000)
TUNABLE_PARAM(Score, bad_underpromotion_score, -500000, -1000000, 0)

TUNABLE_PARAM(i32, lmr_base_100, 75, 40, 150)
TUNABLE_PARAM(i32, lmr_divisor_100, 225, 150, 400)
TUNABLE_PARAM(i32, lmr_history_divisor, 4000, 2000, 8000)
TUNABLE_PARAM(i32, lmr_improving_reduction, 1, -2, 2)
TUNABLE_PARAM(i32, lmr_capture_moves, 4, 2, 8)

TUNABLE_PARAM(i32, history_bonus_max, 400, 200, 1000)
TUNABLE_PARAM(i32, history_bonus_mult, 4, 1, 8)
TUNABLE_PARAM(i32, cont_hist_1_weight, 100, 20, 200)
TUNABLE_PARAM(i32, cont_hist_2_weight, 50, 10, 100)

TUNABLE_PARAM(i32, capture_history_bonus_max, 300, 100, 1000)
TUNABLE_PARAM(i32, capture_history_bonus_mult, 4, 1, 10)

TUNABLE_PARAM(i32, rfp_max_depth, 7, 3, 9)
TUNABLE_PARAM(i32, rfp_multiplier, 75, 30, 120)
TUNABLE_PARAM(Score, rfp_improving_margin_bonus, 50, -100, 200)

TUNABLE_PARAM(i32, nmp_min_depth, 3, 1, 5)
TUNABLE_PARAM(i32, nmp_base_r, 3, 1, 6)
TUNABLE_PARAM(i32, nmp_depth_divisor, 4, 2, 8)
TUNABLE_PARAM(i32, nmp_margin_divisor, 200, 100, 400)
TUNABLE_PARAM(i32, nmp_max_eval_r, 3, 1, 4)

TUNABLE_PARAM(i32, fp_max_depth, 6, 3, 9)
TUNABLE_PARAM(i32, fp_multiplier, 120, 50, 200)
TUNABLE_PARAM(Score, fp_improving_margin_bonus, 50, -100, 200)

TUNABLE_PARAM(Score, delta_margin, 200, 0, 400)
TUNABLE_PARAM(i32, iir_min_depth, 4, 2, 8)
TUNABLE_PARAM(i32, iir_reduction, 1, 1, 3)

TUNABLE_PARAM(i32, lmp_max_depth, 8, 3, 12)
TUNABLE_PARAM(i32, lmp_base, 3, 1, 10)
TUNABLE_PARAM(i32, lmp_multiplier, 12, 3, 30)

TUNABLE_PARAM(i32, hp_max_depth, 4, 1, 8)
TUNABLE_PARAM(i32, hp_margin, 2000, 500, 5000)

TUNABLE_PARAM(i32, se_min_depth, 8, 4, 12)
TUNABLE_PARAM(i32, se_depth_reduction, 4, 2, 8)
TUNABLE_PARAM(Score, se_margin, 20, 0, 80)
TUNABLE_PARAM(i32, se_extension, 1, 1, 3)
TUNABLE_PARAM(i32, se_double_ext_cap, 3, 1, 5)
TUNABLE_PARAM(i32, se_negative_extension_depth, 1, 0, 3)
TUNABLE_PARAM(Score, se_double_ext_margin, 20, 0, 100)

TUNABLE_PARAM(i32, pc_min_depth, 5, 3, 9)
TUNABLE_PARAM(i32, pc_depth_reduction, 4, 2, 8)
TUNABLE_PARAM(Score, pc_margin, 200, 100, 400)

TUNABLE_PARAM(i32, razoring_max_depth, 3, 1, 6)
TUNABLE_PARAM(Score, razoring_margin, 200, 50, 400)

TUNABLE_PARAM(i32, ch_weight, 16, 1, 32)
TUNABLE_PARAM(Score, ch_cap, 40, 20, 70)

TUNABLE_PARAM(Score, see_capture_margin, -50, -200, 0)
TUNABLE_PARAM(Score, see_quiet_margin, -50, -200, -5)
TUNABLE_PARAM(i32, see_pruning_max_depth, 8, 2, 12)

#ifdef TUNE_BUILD
inline void print_optuna_json()
{
    std::cout << "{\n";
    for (usize i = 0; i < Params::tunable_registry().size(); ++i)
    {
        const auto& p = Params::tunable_registry()[i];

        std::cout << "  \"" << p.name << "\": {\"default\": " << *p.ptr << ", \"min\": " << p.min
                  << ", \"max\": " << p.max << ", \"step\": " << 1 << "}";

        if (i < Params::tunable_registry().size() - 1) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "}\n";
}
#endif

void init_lmr_table();

} // namespace Params

inline i64 now_ms()
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

struct SearchState
{
    i64 nodes = 0;
    TranspositionTable* tt = nullptr;
    i64 start_time = 0;
    i64 hard_time_limit_ms = 999999999;
    std::atomic_bool* stop = nullptr;

    // Killer moves (2 slots per ply).
    Move killers[MAX_PLY][2] {};

    // History heuristic ([color][from_to] butterfly table).
    i32 history[2][4096] {};

    // Countermove heuristic ([piece][to_square] -> move that refuted it).
    Move countermoves[16][64] {};

    // Continuation history ([prev_piece][prev_to][piece][to]).
    // 1-ply (indexed by previous move) and 2-ply (indexed by move two plies ago).
    i16 cont_history[2][16][64][16][64] {};

    // Correction history ([color][pawn_hash % 16384]).
    i16 correction_history[2][16384] {};

    // Capture history ([piece][to][captured_piece_type]).
    i16 capture_history[16][64][8] {};

    // Eval tracking for improving context.
    Score evals[MAX_PLY] {};

    void clear_heuristics()
    {
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        std::memset(countermoves, 0, sizeof(countermoves));
        std::memset(cont_history, 0, sizeof(cont_history));
        std::memset(correction_history, 0, sizeof(correction_history));
        std::memset(capture_history, 0, sizeof(capture_history));
    }

    bool time_up()
    {
        if (!stop) return false;
        if (stop->load(std::memory_order_relaxed)) return true;
        if ((nodes & 2047) == 0)
        {
            if (now_ms() - start_time >= hard_time_limit_ms)
            {
                stop->store(true, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }
};

struct RootResult
{
    Score score;
    Move best_move;
    bool completed;
};

RootResult search_root(Board& board, i32 depth, Score alpha, Score beta, SearchState& state);

inline void iterative_deepening(Board& board, i32 max_depth, i64 hard_limit_ms, i64 soft_limit_ms, SearchState* state)
{
    state->nodes = 0;
    state->start_time = now_ms();
    state->hard_time_limit_ms = hard_limit_ms;

    Move best_move = Move {};
    Score score = 0;

    for (i32 depth = 1; depth <= max_depth; depth++)
    {
        i32 delta = 25;
        Score alpha = -INF;
        Score beta = INF;

        if (depth >= 4 && std::abs(score) < MATE_THRESHOLD)
        {
            alpha = std::max(-INF, score - delta);
            beta = std::min(INF, score + delta);
        }

        Move iter_best_move = Move {};
        Score iter_score = 0;
        bool iter_completed = false;

        while (true)
        {
            RootResult res = search_root(board, depth, alpha, beta, *state);
            if (!res.completed) break;

            iter_score = res.score;
            iter_best_move = res.best_move;

            if (iter_score <= alpha)
            {
                alpha = std::max(-INF, alpha - delta);
                delta += delta / 2;
            }
            else if (iter_score >= beta)
            {
                beta = std::min(INF, beta + delta);
                delta += delta / 2;
            }
            else
            {
                iter_completed = true;
                break; // Score is within window and depth search is completed.
            }
        }

        if (state->stop && state->stop->load(std::memory_order_relaxed))
        {
            if (depth == 1 && best_move.is_null())
            {
                MoveList ml = board.generate_moves<GenType::ALL>();
                best_move = iter_best_move.is_null() ? (ml.empty() ? Move {} : ml[0].move) : iter_best_move;
            }
            break;
        }

        if (!iter_completed) break;

        best_move = iter_best_move;
        score = iter_score;

        i64 elapsed = now_ms() - state->start_time;
        i64 nps = elapsed > 0 ? (state->nodes * 1000) / elapsed : 0;

        std::string score_str;
        if (score > MATE_THRESHOLD)
        {
            int plies = std::max(1, MATE_VALUE - score);
            score_str = "mate " + std::to_string((plies + 1) / 2);
        }
        else if (score < -MATE_THRESHOLD)
        {
            int plies = std::max(1, score - (-MATE_VALUE));
            score_str = "mate -" + std::to_string((plies + 1) / 2);
        }
        else
        {
            score_str = "cp " + std::to_string(score);
        }

        std::cout << "info depth " << depth << " score " << score_str << " nodes " << state->nodes << " nps " << nps
                  << " time " << elapsed << " pv";

        Board pv_board = board;
        for (i32 i = 0; i < depth; i++)
        {
            Move pv_move = Move {};
            if (i == 0)
                pv_move = best_move;
            else if (auto entry = state->tt->probe(pv_board.zobrist_key(), 0))
                pv_move = entry->move;

            if (pv_move.is_null()) break;

            MoveList ml = pv_board.generate_moves<GenType::ALL>();
            bool valid = false;
            for (ScoredMove sm : ml)
            {
                if (sm.move == pv_move)
                {
                    valid = true;
                    break;
                }
            }
            if (!valid) break;

            std::cout << " " << pv_move.to_uci();
            pv_board.make_move(pv_move);
            if (pv_board.is_draw(i + 1)) break;
        }
        std::cout << std::endl;

        if (state->time_up()) break;
        if (now_ms() - state->start_time >= soft_limit_ms) break;
    }

    if (best_move.is_null())
    {
        MoveList ml = board.generate_moves<GenType::ALL>();
        if (!ml.empty()) best_move = ml[0].move;
    }

    std::cout << "bestmove " << best_move.to_uci() << std::endl;
}