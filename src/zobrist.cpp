#include "zobrist.hpp"
#include "board.hpp"

namespace zobrist
{
static u64 PIECE_KEYS[12][64];
static u64 CASTLING_KEYS[16];
static u64 EP_KEYS[64];
static u64 SIDE_KEY;

class PRNG
{
    u64 s;

public:

    PRNG(u64 seed) : s(seed) {}
    u64 rand()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
};

void init()
{
    PRNG rng(22122003);

    for (int p = 0; p < 12; p++)
        for (int sq = 0; sq < 64; sq++) PIECE_KEYS[p][sq] = rng.rand();

    for (int i = 0; i < 16; i++) CASTLING_KEYS[i] = rng.rand();

    for (int sq = 0; sq < 64; sq++) EP_KEYS[sq] = rng.rand();

    SIDE_KEY = rng.rand();
}

u64 get_piece_key(Piece p, Square sq)
{
    if (p == EMPTY) return 0;

    usize type_idx = static_cast<usize>(get_piece_type(p)) - 1;
    usize color_idx = static_cast<usize>(get_piece_color(p));
    usize p_idx = type_idx * 2 + color_idx;

    return PIECE_KEYS[p_idx][sq];
}

u64 get_castling_key(CastlingRights cr)
{
    return CASTLING_KEYS[static_cast<u8>(cr) & 0xF];
}

u64 get_ep_key(Square sq)
{
    if (sq == NO_SQUARE) return 0;
    return EP_KEYS[sq];
}

u64 get_side_key()
{
    return SIDE_KEY;
}

u64 compute_hash(const Board& board)
{
    u64 h = 0;
    for (Square sq = 0; sq < 64; sq++)
        if (board.pieces[sq] != EMPTY) h ^= get_piece_key(board.pieces[sq], sq);

    if (board.side_to_move == Color::BLACK) h ^= get_side_key();

    h ^= get_castling_key(board.history[board.ply].castling_rights);
    h ^= get_ep_key(board.history[board.ply].ep_square);

    return h;
}
} // namespace zobrist
