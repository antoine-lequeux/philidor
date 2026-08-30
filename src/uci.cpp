#include "uci.hpp"
#include "board.hpp"
#include "defines.hpp"
#include "movelist.hpp"
#include "search.hpp"
#include "tt.hpp"

#include <atomic>
#include <iostream>
#include <iterator>
#include <print>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::atomic_bool stop_search {false};
static std::thread search_thread;

static void run_search(Board board, i32 max_depth, i64 hard_limit_ms, i64 soft_limit_ms, TranspositionTable* tt)
{
    iterative_deepening(board, max_depth, hard_limit_ms, soft_limit_ms, tt, &stop_search);
}

inline u64 count_nodes(Board& board, u64 depth)
{
    MoveList mvl = board.generate_moves<GenType::ALL>();
    if (depth <= 1) return mvl.size();
    u64 nodes = 0;
    for (ScoredMove sm : mvl)
    {
        board.make_move(sm.move);
        nodes += count_nodes(board, depth - 1);
        board.unmake_move(sm.move);
    }
    return nodes;
}

inline void launch_perft(Board& board, u64 max_depth)
{
    MoveList mvl = board.generate_moves<GenType::ALL>();
    const auto start = std::chrono::steady_clock::now();
    u64 total = 0;
    for (ScoredMove sm : mvl)
    {
        board.make_move(sm.move);
        u64 nodes = count_nodes(board, max_depth - 1);
        board.unmake_move(sm.move);
        total += nodes;
        std::println("{}: {:L}", sm.move.to_uci(), nodes);
    }
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> duration_seconds = end - start;
    const double seconds = duration_seconds.count();
    const double milliseconds = duration_seconds.count() * 1000.0;
    const double nps = static_cast<double>(total) / seconds;

    std::println();
    std::println("Nodes searched: {:L}", total);
    std::println("Time: {:.0f}ms", milliseconds);
    std::println("NPS: {:L}", static_cast<u32>(nps));
}

inline void launch_enum(Board& board, u64 max_depth)
{
    for (u64 d = 1; d <= max_depth; d++)
    {
        const auto start = std::chrono::steady_clock::now();
        u64 nodes = count_nodes(board, d);
        const auto end = std::chrono::steady_clock::now();
        const std::chrono::duration<double> duration_seconds = end - start;
        const double milliseconds = duration_seconds.count() * 1000.0;
        std::println("Depth: {}, positions: {:L}, time: {:.0f}ms", d, nodes, milliseconds);
    }
}

void uci_loop()
{
    Board board = Board::from_startpos();
    auto tt = std::make_unique<TranspositionTable>(64);

    std::string line;
    while (std::getline(std::cin, line))
    {
        std::istringstream iss(line);
        std::vector<std::string> tokens {
            std::istream_iterator<std::string> {iss}, std::istream_iterator<std::string> {}
        };
        if (tokens.empty()) continue;

        const std::string& cmd = tokens[0];

        if (cmd == "uci")
        {
            std::cout << "id name Philidor\n";
            std::cout << "id author Antoine Lequeux\n";
            std::cout << "option name Hash type spin default 64 min 1 max 65536\n";
            std::cout << "uciok\n" << std::flush;
        }
        else if (cmd == "isready")
        {
            std::cout << "readyok\n" << std::flush;
        }
        else if (cmd == "setoption")
        {
            if (tokens.size() >= 5 && tokens[1] == "name" && tokens[2] == "Hash" && tokens[3] == "value")
            {
                usize mb = std::stoull(tokens[4]);
                tt = std::make_unique<TranspositionTable>(mb);
            }
        }
        else if (cmd == "ucinewgame")
        {
            tt->clear();
            board = Board::from_startpos();
        }
        else if (cmd == "position")
        {
            if (tokens.size() >= 2)
            {
                usize moves_idx = 0;
                if (tokens[1] == "startpos")
                {
                    board = Board::from_startpos();
                    moves_idx = 2;
                }
                else if (tokens[1] == "fen" && tokens.size() >= 6)
                {
                    std::string fen = "";
                    moves_idx = 2;
                    while (moves_idx < tokens.size() && tokens[moves_idx] != "moves")
                    {
                        fen += tokens[moves_idx] + " ";
                        moves_idx++;
                    }
                    if (auto res = Board::from_fen(fen)) board = res.value();
                }

                if (moves_idx < tokens.size() && tokens[moves_idx] == "moves")
                {
                    moves_idx++;
                    for (usize i = moves_idx; i < tokens.size(); i++)
                    {
                        MoveList ml = board.generate_moves<GenType::ALL>();
                        for (ScoredMove sm : ml)
                        {
                            if (sm.move.to_uci() == tokens[i])
                            {
                                board.make_move(sm.move);
                                break;
                            }
                        }
                    }
                }
            }
        }
        else if (cmd == "eval")
        {
            std::println("Score: {}", board.evaluate());
        }
        else if (cmd == "d" || cmd == "display")
        {
            board.display();
        }
        else if (cmd == "go")
        {
            if (search_thread.joinable())
            {
                stop_search = true;
                search_thread.join();
            }

            tt->new_search();
            stop_search = false;

            i32 max_depth = 64;
            u64 time_limit_ms = 0;

            u64 wtime = 0, btime = 0, winc = 0, binc = 0;
            bool infinite = false;

            bool skip_search = false;
            for (usize i = 1; i < tokens.size(); i++)
                if (tokens[i] == "perft" && i + 1 < tokens.size())
                {
                    launch_perft(board, static_cast<u64>(std::stoi(tokens[++i])));
                    skip_search = true;
                }
                else if (tokens[i] == "enum" && i + 1 < tokens.size())
                {
                    launch_enum(board, static_cast<u64>(std::stoi(tokens[++i])));
                    skip_search = true;
                }
                else if (tokens[i] == "depth" && i + 1 < tokens.size())
                    max_depth = std::stoi(tokens[++i]);
                else if (tokens[i] == "wtime" && i + 1 < tokens.size())
                    wtime = std::stoull(tokens[++i]);
                else if (tokens[i] == "btime" && i + 1 < tokens.size())
                    btime = std::stoull(tokens[++i]);
                else if (tokens[i] == "winc" && i + 1 < tokens.size())
                    winc = std::stoull(tokens[++i]);
                else if (tokens[i] == "binc" && i + 1 < tokens.size())
                    binc = std::stoull(tokens[++i]);
                else if (tokens[i] == "movetime" && i + 1 < tokens.size())
                {
                    time_limit_ms = std::stoull(tokens[++i]);
                }
                else if (tokens[i] == "infinite")
                    infinite = true;

            i64 hard_limit_ms = 999999999;
            i64 soft_limit_ms = 999999999;

            if (!infinite && time_limit_ms > 0)
            {
                hard_limit_ms = static_cast<i64>(time_limit_ms);
                soft_limit_ms = static_cast<i64>(time_limit_ms);
            }
            else if (!infinite && (wtime > 0 || btime > 0))
            {
                u64 our_time = board.side_to_move == Color::WHITE ? wtime : btime;
                u64 our_inc = board.side_to_move == Color::WHITE ? winc : binc;

                u64 base = our_time / 30 + (our_inc * 3 / 4);
                soft_limit_ms = static_cast<i64>(base);
                hard_limit_ms = static_cast<i64>(std::min(base * 3, our_time >= 50 ? our_time - 50 : 0));
            }

            if (!skip_search)
                search_thread = std::thread(run_search, board, max_depth, hard_limit_ms, soft_limit_ms, tt.get());
        }
        else if (cmd == "stop")
        {
            if (search_thread.joinable())
            {
                stop_search = true;
                search_thread.join();
            }
        }
        else if (cmd == "quit")
        {
            if (search_thread.joinable())
            {
                stop_search = true;
                search_thread.join();
            }
            break;
        }
        else if (cmd == "d")
        {
            board.display();
        }
    }

    if (search_thread.joinable())
    {
        stop_search = true;
        search_thread.join();
    }
}
