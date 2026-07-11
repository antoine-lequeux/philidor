#pragma once

#include "movegen.hpp"

#include <chrono>
#include <print>

usize count_nodes(Board& board, usize depth)
{
    MoveList mvl;
    generate_moves(board, mvl);
    if (depth <= 1)
        return mvl.size();
    usize nodes = 0;
    for (Move mv : mvl)
    {
        board.make_move(mv);
        nodes += count_nodes(board, depth - 1);
        board.unmake_move(mv);
    }
    return nodes;
}

void launch_perft(Board& board, usize max_depth)
{
    MoveList mvl;
    generate_moves(board, mvl);
    const auto start = std::chrono::steady_clock::now();
    usize total = 0;
    for (Move mv : mvl)
    {
        board.make_move(mv);
        usize nodes = count_nodes(board, max_depth - 1);
        board.unmake_move(mv);
        total += nodes;
        std::println("{}: {:L}", mv.to_uci(), nodes);
    }
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> duration_seconds = end - start;
    const double seconds = duration_seconds.count();
    const double nps = static_cast<double>(total) / seconds;

    std::println();
    std::println("Nodes searched: {:L}", total);
    std::println("Time: {:.2f} s", seconds);
    std::println("NPS: {:L}", static_cast<u32>(nps));
}