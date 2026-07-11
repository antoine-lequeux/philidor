#include "board.hpp"
#include "magic.hpp"
#include "uci.hpp"

int main()
{
    std::locale::global(std::locale("en_US.UTF-8"));
    init_magic();

    auto result = Board::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    if (!result)
    {
        std::cerr << "[FEN Error]: " << result.error() << "\n";
        return 1;
    }

    Board board = *result;
    board.display();
    launch_perft(board, 7);

    return 0;
}