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
constexpr Score tt_move_score = 29149;
constexpr Score promotion_bonus = 20386;
constexpr Score good_capture = 9743;
constexpr Score killer_score_0 = 8818;
constexpr Score killer_score_1 = 7732;
constexpr Score countermove_score = 6930;
constexpr Score bad_capture = -5409;

constexpr f64 lmr_base = 0.877806;
constexpr f64 lmr_divisor = 2.3931;
constexpr i32 lmr_history_divisor = 3904;

constexpr i32 history_bonus_max = 401;
constexpr i32 history_bonus_mult = 1;

constexpr i32 rfp_max_depth = 4;
constexpr i32 rfp_multiplier = 72;

constexpr i32 nmp_min_depth = 3;
constexpr i32 nmp_base_r = 3;
constexpr i32 nmp_depth_divisor = 6;

constexpr i32 fp_max_depth = 4;
constexpr i32 fp_multiplier = 155;
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