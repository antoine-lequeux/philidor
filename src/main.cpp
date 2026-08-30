#include "magic.hpp"
#include "uci.hpp"
#include "zobrist.hpp"

int main()
{
    init_magic();
    zobrist::init();
    uci_loop();

    return 0;
}