#pragma once

#include "movegen.hpp"

#include <chrono>
#include <print>

inline u64 count_nodes(Board& board, u32 depth)
{
    MoveList mvl;
    generate_moves(board, mvl);
    if (depth <= 1) return mvl.size();
    u64 nodes = 0;
    for (Move mv : mvl)
    {
        board.make_move(mv);
        nodes += count_nodes(board, depth - 1);
        board.unmake_move(mv);
    }
    return nodes;
}

inline void launch_perft(Board& board, u32 max_depth)
{
    MoveList mvl;
    generate_moves(board, mvl);
    const auto start = std::chrono::steady_clock::now();
    u64 total = 0;
    for (Move mv : mvl)
    {
        board.make_move(mv);
        u64 nodes = count_nodes(board, max_depth - 1);
        board.unmake_move(mv);
        total += nodes;
        std::println("{}: {}", mv.to_uci(), nodes);
    }
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<f64> duration_seconds = end - start;
    const f64 seconds = duration_seconds.count();
    const f64 milliseconds = duration_seconds.count() * 1000.0;
    const f64 nps = static_cast<f64>(total) / seconds;

    std::println();
    std::println("Nodes searched: {}", total);
    std::println("Time: {:.0f}ms", milliseconds);
    std::println("NPS: {}", static_cast<u32>(nps));
}

inline void launch_enum(Board& board, u32 max_depth)
{
    for (u32 d = 1; d <= max_depth; d++)
    {
        const auto start = std::chrono::steady_clock::now();
        u64 nodes = count_nodes(board, d);
        const auto end = std::chrono::steady_clock::now();
        const std::chrono::duration<f64> duration_seconds = end - start;
        const f64 milliseconds = duration_seconds.count() * 1000.0;
        std::println("Depth: {}, positions: {}, time: {:.0f}ms", d, nodes, milliseconds);
    }
}