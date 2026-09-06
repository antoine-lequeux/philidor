#include "magic.hpp"
#include "nnue.hpp"
#include "uci.hpp"
#include "zobrist.hpp"

int main()
{
    init_magic();
    zobrist::init();
    NNUE::init();
    uci_loop();

    return 0;
}