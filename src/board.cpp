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

Board::Board() : history(std::make_unique<std::array<State, 512>>()) {}

Board::~Board() = default;

Board::Board(const Board& other) : history(std::make_unique<std::array<State, 512>>(*other.history))
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
}

Board& Board::operator=(const Board& other)
{
    if (this != &other)
    {
        *history = *other.history;
        pieces = other.pieces;
        kings = other.kings;
        piece_bb = other.piece_bb;
        color_bb = other.color_bb;
        ortho_sliders = other.ortho_sliders;
        diag_sliders = other.diag_sliders;
        occupancy = other.occupancy;
        side_to_move = other.side_to_move;
        ply = other.ply;
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
    (*board.history)[0].castling_rights = rights;

    if (ep == "-")
    {
        (*board.history)[0].ep_square = NO_SQUARE;
    }
    else
    {
        if (ep.length() != 2) return std::unexpected("Invalid en passant square format.");
        char f = ep[0];
        char r = ep[1];
        if (f < 'a' || f > 'h' || r < '1' || r > '8') return std::unexpected("Invalid en passant square coordinates.");

        (*board.history)[0].ep_square = static_cast<Square>((r - '1') * 8 + (f - 'a'));
    }

    u16 clock_val = 0;
    auto [ptr, ec] = std::from_chars(halfmove.data(), halfmove.data() + halfmove.size(), clock_val);

    if (ec != std::errc {} || ptr != halfmove.data() + halfmove.size())
        return std::unexpected("Invalid halfmove clock provided in FEN string.");

    (*board.history)[0].halfmove_clock = clock_val;
    (*board.history)[0].hash = zobrist::compute_hash(board);

    NNUE::update_full((*board.history)[0].acc, board);

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

bool Board::is_pseudo_legal(Move m) const
{
    if (m.is_null()) return false;

    const Square from = m.get_start_square();
    const Square to = m.get_target_square();

    if (from >= 64 || to >= 64 || from == to) return false;

    const Piece pc = pieces[from];
    if (pc == EMPTY || get_piece_color(pc) != side_to_move) return false;

    const Piece dest = pieces[to];
    if (dest != EMPTY)
    {
        if (get_piece_color(dest) == side_to_move) return false;
        if (get_piece_type(dest) == Type::KING) return false;
    }

    const Type pt = get_piece_type(pc);
    const u16 flag = m.get_flag();
    const Color us = side_to_move;

    if (pt == Type::PAWN)
    {
        const u64 to_bb = 1ULL << to;
        const u64 promo_rank = (us == Color::WHITE) ? RANK_8 : RANK_1;
        const bool is_promo = (to_bb & promo_rank) != 0;

        if (is_promo)
        {
            if (flag != Move::PROMOTE_TO_QUEEN_FLAG && flag != Move::PROMOTE_TO_KNIGHT_FLAG &&
                flag != Move::PROMOTE_TO_ROOK_FLAG && flag != Move::PROMOTE_TO_BISHOP_FLAG)
                return false;
        }
        else
        {
            if (flag != 0 && flag != Move::ENPASSANT_CAPTURE_FLAG && flag != Move::PAWN_TWO_UP_FLAG) return false;
        }

        if (flag == Move::ENPASSANT_CAPTURE_FLAG)
        {
            const Square ep_sq = (*history)[ply].ep_square;
            if (to != ep_sq || ep_sq == NO_SQUARE) return false;
            return (pawn_attacks(from, us) & to_bb) != 0;
        }

        if (flag == Move::PAWN_TWO_UP_FLAG)
        {
            if (dest != EMPTY) return false;
            if (us == Color::WHITE)
            {
                if (from / 8 != 1 || to != from + 16) return false;
                if (pieces[from + 8] != EMPTY) return false;
            }
            else
            {
                if (from / 8 != 6 || to != from - 16) return false;
                if (pieces[from - 8] != EMPTY) return false;
            }
            return true;
        }

        const Square push_sq = (us == Color::WHITE) ? from + 8 : from - 8;
        if (to == push_sq) return dest == EMPTY;

        if (pawn_attacks(from, us) & to_bb) return dest != EMPTY;

        return false;
    }

    if (m.is_promotion()) return false;
    if (flag != 0 && (pt != Type::KING || flag != Move::CASTLE_FLAG)) return false;

    if (pt == Type::KNIGHT) return (knight_attacks(from) & (1ULL << to)) != 0;

    if (pt == Type::BISHOP) return (bishop_attacks(from, occupancy) & (1ULL << to)) != 0;

    if (pt == Type::ROOK) return (rook_attacks(from, occupancy) & (1ULL << to)) != 0;

    if (pt == Type::QUEEN)
        return ((bishop_attacks(from, occupancy) | rook_attacks(from, occupancy)) & (1ULL << to)) != 0;

    if (pt == Type::KING)
    {
        if (flag == Move::CASTLE_FLAG)
        {
            const CastlingRights cr = (*history)[ply].castling_rights;
            if (in_check()) return false;

            if (us == Color::WHITE)
            {
                if (from != 4) return false;
                if (to == 6)
                {
                    if ((cr & CastlingRights::WK) == CastlingRights::NONE) return false;
                    if (occupancy & WHITE_OO_BLOCKERS) return false;
                    if (is_attacked_by<Color::BLACK>(*this, 5, occupancy)) return false;
                    if (is_attacked_by<Color::BLACK>(*this, 6, occupancy)) return false;
                    return true;
                }
                if (to == 2)
                {
                    if ((cr & CastlingRights::WQ) == CastlingRights::NONE) return false;
                    if (occupancy & WHITE_OOO_BLOCKERS) return false;
                    if (is_attacked_by<Color::BLACK>(*this, 3, occupancy)) return false;
                    if (is_attacked_by<Color::BLACK>(*this, 2, occupancy)) return false;
                    return true;
                }
            }
            else
            {
                if (from != 60) return false;
                if (to == 62)
                {
                    if ((cr & CastlingRights::BK) == CastlingRights::NONE) return false;
                    if (occupancy & BLACK_OO_BLOCKERS) return false;
                    if (is_attacked_by<Color::WHITE>(*this, 61, occupancy)) return false;
                    if (is_attacked_by<Color::WHITE>(*this, 62, occupancy)) return false;
                    return true;
                }
                if (to == 58)
                {
                    if ((cr & CastlingRights::BQ) == CastlingRights::NONE) return false;
                    if (occupancy & BLACK_OOO_BLOCKERS) return false;
                    if (is_attacked_by<Color::WHITE>(*this, 59, occupancy)) return false;
                    if (is_attacked_by<Color::WHITE>(*this, 58, occupancy)) return false;
                    return true;
                }
            }
            return false;
        }

        return (king_attacks(from) & (1ULL << to)) != 0;
    }

    return false;
}

bool Board::is_legal(Move m)
{
    if (!is_pseudo_legal(m)) return false;
    make_move(m);
    bool legal = !is_in_check(*this, !side_to_move);
    unmake_move(m);
    return legal;
}

bool Board::is_draw(i32 search_ply) const
{
    if ((*history)[ply].halfmove_clock >= 100) return true;
    if (ply < 2) return false;

    u64 current_hash = (*history)[ply].hash;
    u32 limit = ply > (*history)[ply].halfmove_clock ? ply - (*history)[ply].halfmove_clock : 0;
    u32 root_ply = ply >= static_cast<u32>(search_ply) ? ply - static_cast<u32>(search_ply) : 0;

    int count = 0;
    for (u32 p = ply - 2; p >= limit; p -= 2)
    {
        if ((*history)[p].hash == current_hash)
        {
            if (p >= root_ply) return true;
            count++;
            if (count >= 2) return true;
        }
        if (p < 2) break;
    }
    return false;
}

Score Board::evaluate() const
{
    return NNUE::evaluate((*history)[ply].acc, side_to_move);
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

    State& current_state = (*history)[ply];
    State& next_state = (*history)[ply + 1];

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