#pragma once

#include "board.hpp"
#include "defines.hpp"
#include "movelist.hpp"
#include "tt.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>

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

TUNABLE_PARAM(Score, tt_move_score, 16321, 12000, 22000)
TUNABLE_PARAM(Score, promotion_bonus, 18195, 10000, 20000)
TUNABLE_PARAM(Score, good_capture, 13164, 12000, 17000)
TUNABLE_PARAM(Score, killer_score_0, 4734, 4000, 9000)
TUNABLE_PARAM(Score, killer_score_1, 5568, 5000, 10000)
TUNABLE_PARAM(Score, countermove_score, 8552, 5000, 13000)
TUNABLE_PARAM(Score, bad_capture, -8519, -12000, -5000)

TUNABLE_PARAM(i32, lmr_base_100, 144, 80, 160)
TUNABLE_PARAM(i32, lmr_divisor_100, 347, 200, 500)
TUNABLE_PARAM(i32, lmr_history_divisor, 5858, 3000, 6000)

TUNABLE_PARAM(i32, history_bonus_max, 710, 300, 900)
TUNABLE_PARAM(i32, history_bonus_mult, 5, 3, 7)

TUNABLE_PARAM(i32, rfp_max_depth, 3, 2, 6)
TUNABLE_PARAM(i32, rfp_multiplier, 33, 20, 70)

TUNABLE_PARAM(i32, nmp_min_depth, 2, 1, 4)
TUNABLE_PARAM(i32, nmp_base_r, 5, 1, 8)
TUNABLE_PARAM(i32, nmp_depth_divisor, 2, 2, 8)

TUNABLE_PARAM(i32, fp_max_depth, 5, 3, 9)
TUNABLE_PARAM(i32, fp_multiplier, 69, 30, 150)

TUNABLE_PARAM(Score, delta_margin, 122, 0, 300)
TUNABLE_PARAM(i32, iir_min_depth, 3, 1, 8)
TUNABLE_PARAM(i32, iir_reduction, 1, 1, 3)

TUNABLE_PARAM(i32, lmp_max_depth, 8, 1, 10)
TUNABLE_PARAM(i32, lmp_base, 6, 0, 10)
TUNABLE_PARAM(i32, lmp_multiplier, 2, 0, 20)

TUNABLE_PARAM(i32, se_min_depth, 10, 4, 12)
TUNABLE_PARAM(i32, se_depth_reduction, 3, 1, 6)
TUNABLE_PARAM(Score, se_margin, 77, 0, 100)
TUNABLE_PARAM(i32, se_extension, 1, 1, 3)
TUNABLE_PARAM(i32, se_double_ext_cap, 2, 1, 5)

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

void init_lmr_table();
#endif

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

    void clear_heuristics()
    {
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        std::memset(countermoves, 0, sizeof(countermoves));
        std::memset(cont_history, 0, sizeof(cont_history));
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

inline void iterative_deepening(
    Board& board, i32 max_depth, i64 hard_limit_ms, i64 soft_limit_ms, TranspositionTable* tt, std::atomic_bool* stop
)
{
    auto state = std::make_unique<SearchState>();
    state->start_time = now_ms();
    state->hard_time_limit_ms = hard_limit_ms;
    state->tt = tt;
    state->stop = stop;
    state->clear_heuristics();

    Move best_move = Move {};
    Score score = 0;

    for (i32 depth = 1; depth <= max_depth; depth++)
    {
        i32 delta = 25;
        Score alpha = -INF;
        Score beta = INF;

        if (depth >= 4)
        {
            alpha = score - delta;
            beta = score + delta;
        }

        while (true)
        {
            RootResult res = search_root(board, depth, alpha, beta, *state);
            if (!res.completed)
            {
                if (!res.best_move.is_null() && res.score > score)
                {
                    score = res.score;
                    best_move = res.best_move;
                }
                break;
            }

            score = res.score;
            best_move = res.best_move;

            if (score <= alpha)
            {
                beta = (alpha + beta) / 2;
                alpha = std::max(-INF, alpha - delta);
                delta += delta / 2;
            }
            else if (score >= beta)
            {
                beta = std::min(INF, beta + delta);
                delta += delta / 2;
            }
            else
            {
                break; // Score is within window.
            }
        }

        if (state->time_up()) break;

        i64 elapsed = now_ms() - state->start_time;
        i64 nps = elapsed > 0 ? (state->nodes * 1000) / elapsed : 0;

        std::string score_str;
        if (score > MATE_THRESHOLD)
        {
            int plies = MATE_VALUE - score;
            score_str = "mate " + std::to_string((plies + 1) / 2);
        }
        else if (score < -MATE_THRESHOLD)
        {
            int plies = MATE_VALUE + score;
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
            if (auto entry = tt->probe(pv_board.zobrist_key(), 0))
            {
                Move pv_move = entry->move;
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
            }
            else
            {
                break;
            }
        }
        std::cout << std::endl;

        if (now_ms() - state->start_time >= soft_limit_ms) break;
    }

    std::cout << "bestmove " << best_move.to_uci() << std::endl;
}