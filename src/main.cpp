#include "board.hpp"
#include "magic.hpp"

int main()
{
    init_magic();

    auto result = Board::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - ");

    if (!result)
    {
        std::cerr << "[FEN Error]: " << result.error() << "\n";
        return 1;
    }

    Board board = *result;
    board.display();

    return 0;
}