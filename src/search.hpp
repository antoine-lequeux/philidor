#pragma once

#include "board.hpp"
#include "defines.hpp"
#include "movelist.hpp"
#include "tt.hpp"

#include <atomic>
#include <chrono>
#include <iostream>

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
    SearchState state;
    state.start_time = now_ms();
    state.hard_time_limit_ms = hard_limit_ms;
    state.tt = tt;
    state.stop = stop;

    Move best_move = Move {};
    Score score = 0;

    for (i32 depth = 1; depth <= max_depth; depth++)
    {
        RootResult res = search_root(board, depth, -INF, INF, state);
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

        i64 elapsed = now_ms() - state.start_time;
        i64 nps = elapsed > 0 ? (state.nodes * 1000) / elapsed : 0;

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

        std::cout << "info depth " << depth << " score " << score_str << " nodes " << state.nodes << " nps " << nps
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

        if (now_ms() - state.start_time >= soft_limit_ms) break;
    }

    std::cout << "bestmove " << best_move.to_uci() << std::endl;
}