#include "board.hpp"
#include "magic.hpp"
#include "uci.hpp"

#include <print>

int main()
{
    init_magic();

    auto result = Board::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    if (!result)
    {
        std::println(stderr, "[FEN Error]: {}", result.error());
        return 1;
    }

    Board board = *result;
    launch_enum(board, 7);
    return 0;
}