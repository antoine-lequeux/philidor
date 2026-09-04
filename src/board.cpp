#include "board.hpp"
#include "defines.hpp"
#include "magic.hpp"
#include "masks.hpp"
#include "movegen.hpp"
#include "nnue.hpp"

#include <charconv>
#include <eve/eve.hpp>
#include <format>
#include <print>

Board::Board() = default;

Board::~Board() = default;

Board::Board(const Board& other)
{
    pieces = other.pieces;
    kings = other.kings;
    piece_bb = other.piece_bb;
    color_bb = other.color_bb;
    ortho_sliders = other.ortho_sliders;
    diag_sliders = other.diag_sliders;
    occupancy = other.occupancy;
    side_to_move = other.side_to_move;
    ply = other.ply;
    history = other.history;
    nnue = other.nnue;
}

Board& Board::operator=(const Board& other)
{
    if (this != &other)
    {
        pieces = other.pieces;
        kings = other.kings;
        piece_bb = other.piece_bb;
        color_bb = other.color_bb;
        ortho_sliders = other.ortho_sliders;
        diag_sliders = other.diag_sliders;
        occupancy = other.occupancy;
        side_to_move = other.side_to_move;
        ply = other.ply;
        history = other.history;
        nnue = other.nnue;
    }
    return *this;
}

Board Board::from_startpos()
{
    return from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").value();
}

static constexpr std::string_view next_token(std::string_view& sv)
{
    while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
    if (sv.empty()) return {};

    usize end = sv.find(' ');
    if (end == std::string_view::npos) end = sv.size();

    std::string_view token = sv.substr(0, end);
    sv.remove_prefix(end);
    return token;
}

std::expected<Board, std::string> Board::from_fen(std::string_view fen)
{
    Board board {};

    std::string_view remaining = fen;
    std::string_view layout = next_token(remaining);
    std::string_view active = next_token(remaining);
    std::string_view castling = next_token(remaining);
    std::string_view ep = next_token(remaining);
    std::string_view halfmove = next_token(remaining);

    if (layout.empty() || active.empty() || castling.empty() || ep.empty())
        return std::unexpected("FEN string is incomplete.");

    if (halfmove.empty()) halfmove = "0";

    u32 rank = 7;
    u32 file = 0;

    for (char ch : layout)
    {
        if (ch == '/')
        {
            if (file != 8) return std::unexpected(std::format("Rank {} has {} squares instead of 8.", rank + 1, file));
            if (rank == 0) return std::unexpected("Too many ranks provided in FEN layout.");
            rank--;
            file = 0;
        }
        else if (ch >= '1' && ch <= '8')
        {
            file += static_cast<u32>(ch - '0');
            if (file > 8) return std::unexpected(std::format("Too many squares on rank {}.", rank + 1));
        }
        else
        {
            if (file >= 8) return std::unexpected(std::format("Too many squares on rank {}.", rank + 1));

            Type type;
            Color color = Color::BLACK;
            switch (ch)
            {
                case 'p': type = Type::PAWN; break;
                case 'n': type = Type::KNIGHT; break;
                case 'b': type = Type::BISHOP; break;
                case 'r': type = Type::ROOK; break;
                case 'q': type = Type::QUEEN; break;
                case 'k': type = Type::KING; break;
                case 'P':
                    type = Type::PAWN;
                    color = Color::WHITE;
                    break;
                case 'N':
                    type = Type::KNIGHT;
                    color = Color::WHITE;
                    break;
                case 'B':
                    type = Type::BISHOP;
                    color = Color::WHITE;
                    break;
                case 'R':
                    type = Type::ROOK;
                    color = Color::WHITE;
                    break;
                case 'Q':
                    type = Type::QUEEN;
                    color = Color::WHITE;
                    break;
                case 'K':
                    type = Type::KING;
                    color = Color::WHITE;
                    break;
                default: return std::unexpected(std::format("Invalid piece character '{}'.", ch));
            }

            Square sq = rank * 8 + file;
            board.put_piece(make_piece(type, color), sq);
            file++;
        }
    }

    if (rank != 0 || file != 8) return std::unexpected("Incomplete FEN board layout.");

    if (active == "w")
        board.side_to_move = Color::WHITE;
    else if (active == "b")
        board.side_to_move = Color::BLACK;
    else
        return std::unexpected("Active color must be 'w' or 'b'.");

    CastlingRights rights = CastlingRights::NONE;
    if (castling != "-")
    {
        for (char c : castling)
        {
            switch (c)
            {
                case 'K': rights |= CastlingRights::WK; break;
                case 'Q': rights |= CastlingRights::WQ; break;
                case 'k': rights |= CastlingRights::BK; break;
                case 'q': rights |= CastlingRights::BQ; break;
                default: return std::unexpected(std::format("Invalid castling right character '{}'.", c));
            }
        }
    }
    board.history[0].castling_rights = rights;

    if (ep == "-")
    {
        board.history[0].ep_square = NO_SQUARE;
    }
    else
    {
        if (ep.length() != 2) return std::unexpected("Invalid en passant square format.");
        char f = ep[0];
        char r = ep[1];
        if (f < 'a' || f > 'h' || r < '1' || r > '8') return std::unexpected("Invalid en passant square coordinates.");

        board.history[0].ep_square = static_cast<Square>((r - '1') * 8 + (f - 'a'));
    }

    u16 clock_val = 0;
    auto [ptr, ec] = std::from_chars(halfmove.data(), halfmove.data() + halfmove.size(), clock_val);

    if (ec != std::errc {} || ptr != halfmove.data() + halfmove.size())
        return std::unexpected("Invalid halfmove clock provided in FEN string.");

    board.history[0].halfmove_clock = clock_val;
    board.history[0].hash = zobrist::compute_hash(board);

    board.nnue.inputs_full_update(0, board.pieces, board.kings);

    return board;
}

void Board::display(bool white_perspective) const
{
    std::string out;
    out.reserve(512);

    out += "  +-----------------+\n";

    constexpr std::array<u32, 8> FORWARD = {0, 1, 2, 3, 4, 5, 6, 7};
    constexpr std::array<u32, 8> REVERSE = {7, 6, 5, 4, 3, 2, 1, 0};

    const auto& ranks = white_perspective ? REVERSE : FORWARD;
    const auto& files = white_perspective ? FORWARD : REVERSE;

    for (u32 rank : ranks)
    {
        out += std::format("{} | ", rank + 1);
        for (u32 file : files)
        {
            Square sq = rank * 8 + file;
            out += piece_to_char(pieces[sq]);
            out += ' ';
        }
        out += "|\n";
    }

    out += "  +-----------------+\n";

    if (white_perspective)
        out += "    a b c d e f g h\n";
    else
        out += "    h g f e d c b a\n";

    std::print("{}", out);
}

bool Board::in_check() const
{
    return is_in_check(*this, side_to_move);
}

bool Board::is_draw() const
{
    if (history[ply].halfmove_clock >= 100) return true;
    if (ply < 2) return false;

    u64 current_hash = history[ply].hash;
    u32 limit = ply > history[ply].halfmove_clock ? ply - history[ply].halfmove_clock : 0;

    for (u32 p = ply - 2; p >= limit; p -= 2)
    {
        if (history[p].hash == current_hash) return true;
        if (p < 2) break;
    }
    return false;
}

Score Board::evaluate() const
{
    return nnue.evaluate(color_index(side_to_move), ply);
}

template <GenType gt>
MoveList Board::generate_moves() const
{
    MoveList ml;
    generate_moves_interface<gt>(*this, ml);
    return ml;
}

template MoveList Board::generate_moves<GenType::CAPTURES>() const;
template MoveList Board::generate_moves<GenType::QUIETS>() const;
template MoveList Board::generate_moves<GenType::ALL>() const;

void Board::make_null()
{
    [[assume(ply < 511)]];

    State& current_state = history[ply];
    State& next_state = history[ply + 1];

    current_state.move = Move();
    current_state.moved_piece = EMPTY;
    current_state.captured_piece = EMPTY;

    next_state = current_state;
    next_state.ep_square = NO_SQUARE;
    next_state.halfmove_clock++;

    u64 hash = current_state.hash;
    if (current_state.ep_square != NO_SQUARE) hash ^= zobrist::get_ep_key(current_state.ep_square);
    hash ^= zobrist::get_side_key();
    next_state.hash = hash;

    nnue.copy_accumulator(ply, ply + 1);

    side_to_move = !side_to_move;
    ply++;
}

void Board::unmake_null()
{
    [[assume(ply > 0)]];
    ply--;
    side_to_move = !side_to_move;
}

Bitboard Board::occupied_by(Color color, Bitboard occ) const
{
    return color_bb[static_cast<u8>(color)] & occ;
}

Bitboard Board::attackers_to(Square sq, Bitboard occ) const
{
    Bitboard attackers = 0;
    attackers |= pawn_attacks(sq, Color::WHITE) & piece_bb[bb_index(Type::PAWN, 1)]; // Black pawns attacking sq
    attackers |= pawn_attacks(sq, Color::BLACK) & piece_bb[bb_index(Type::PAWN, 0)]; // White pawns attacking sq

    attackers |= knight_attacks(sq) & pieces_of_type(Type::KNIGHT);
    attackers |= king_attacks(sq) & pieces_of_type(Type::KING);

    attackers |= bishop_attacks(sq, occ) & (pieces_of_type(Type::BISHOP) | pieces_of_type(Type::QUEEN));
    attackers |= rook_attacks(sq, occ) & (pieces_of_type(Type::ROOK) | pieces_of_type(Type::QUEEN));

    return attackers;
}

Bitboard Board::pieces_of_type(Type type) const
{
    return piece_bb[bb_index(type, 0)] | piece_bb[bb_index(type, 1)];
}