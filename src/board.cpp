#include "board.hpp"

#include <charconv>
#include <format>
#include <sstream>
#include <stdexcept>
#include <vector>

static constexpr std::array<CastlingRights, 64> CASTLING_UPDATE = []
{
    std::array<CastlingRights, 64> arr{};
    for (auto& rights : arr)
        rights = CastlingRights::ALL;
    arr[0] = ~CastlingRights::WQ;                         // a1
    arr[4] = ~(CastlingRights::WK | CastlingRights::WQ);  // e1
    arr[7] = ~CastlingRights::WK;                         // h1
    arr[56] = ~CastlingRights::BQ;                        // a8
    arr[60] = ~(CastlingRights::BK | CastlingRights::BQ); // e8
    arr[63] = ~CastlingRights::BK;                        // h8
    return arr;
}();

void Board::put_piece(Piece p, Square sq) noexcept
{
    pieces[sq] = p;
    const u64 bit = 1ULL << sq;
    const Type pt = get_piece_type(p);
    [[assume(static_cast<u8>(pt) >= 1 && static_cast<u8>(pt) <= 6)]];
    const u8 color_index = static_cast<u8>(get_piece_color(p));
    const u8 bb_index = (static_cast<u8>(pt) - 1) + color_index * 6;

    piece_bb[bb_index] |= bit;
    color_bb[color_index] |= bit;
    if (pt == Type::ROOK || pt == Type::QUEEN)
        ortho_sliders[color_index] |= bit;
    if (pt == Type::BISHOP || pt == Type::QUEEN)
        diag_sliders[color_index] |= bit;
    occupancy |= bit;
}

void Board::remove_piece(Piece p, Square sq) noexcept
{
    pieces[sq] = EMPTY;
    const u64 bit = ~(1ULL << sq);
    const Type pt = get_piece_type(p);
    [[assume(static_cast<u8>(pt) >= 1 && static_cast<u8>(pt) <= 6)]];
    const u8 color_index = static_cast<u8>(get_piece_color(p));
    const u8 bb_index = (static_cast<u8>(pt) - 1) + color_index * 6;

    piece_bb[bb_index] &= bit;
    color_bb[color_index] &= bit;
    if (pt == Type::ROOK || pt == Type::QUEEN)
        ortho_sliders[color_index] &= bit;
    if (pt == Type::BISHOP || pt == Type::QUEEN)
        diag_sliders[color_index] &= bit;
    occupancy &= bit;
}

void Board::move_piece(Piece p, Square from, Square to) noexcept
{
    pieces[from] = EMPTY;
    pieces[to] = p;

    const Type pt = get_piece_type(p);
    [[assume(static_cast<u8>(pt) >= 1 && static_cast<u8>(pt) <= 6)]];
    const u8 color_index = static_cast<u8>(get_piece_color(p));

    const u64 move_mask = (1ULL << from) | (1ULL << to);
    const u8 bb_index = (static_cast<u8>(pt) - 1) + color_index * 6;

    piece_bb[bb_index] ^= move_mask;
    color_bb[color_index] ^= move_mask;
    if (pt == Type::ROOK || pt == Type::QUEEN)
        ortho_sliders[color_index] ^= move_mask;
    if (pt == Type::BISHOP || pt == Type::QUEEN)
        diag_sliders[color_index] ^= move_mask;
    occupancy ^= move_mask;
}

void Board::make_move(Move mv) noexcept
{
    [[assume(ply < 511)]];

    const Square from = mv.get_start_square();
    const Square to = mv.get_target_square();
    const u16 flag = mv.get_flag();

    const Piece moved = pieces[from];

    [[assume(moved != EMPTY)]];

    const Type moved_type = get_piece_type(moved);
    const Color us = side_to_move;

    Piece captured = pieces[to];

    State& current_state = history[ply];
    State& next_state = history[ply + 1];

    next_state = current_state;
    next_state.ep_square = NO_SQUARE;

    current_state.moved_piece = moved;

    if (flag == Move::ENPASSANT_CAPTURE_FLAG)
    {
        Square capture_sq = (us == Color::WHITE) ? to - 8 : to + 8;
        captured = pieces[capture_sq];
        remove_piece(captured, capture_sq);
    }
    else if (captured != EMPTY)
    {
        remove_piece(captured, to);
    }
    current_state.captured_piece = captured;

    if (moved_type == Type::PAWN || captured != EMPTY)
    {
        next_state.halfmove_clock = 0;
    }
    else
    {
        next_state.halfmove_clock++;
    }

    move_piece(moved, from, to);

    if (flag == Move::CASTLE_FLAG)
    {
        const Piece rook = make_piece(Type::ROOK, us);
        switch (to)
        {
            case 6:
                move_piece(rook, 7, 5);
                break; // White Kingside
            case 2:
                move_piece(rook, 0, 3);
                break; // White Queenside
            case 62:
                move_piece(rook, 63, 61);
                break; // Black Kingside
            case 58:
                move_piece(rook, 56, 59);
                break; // Black Queenside
        }
    }
    else if (mv.is_promotion())
    {
        const Piece promoted = make_piece(mv.get_promotion_type(), us);
        remove_piece(moved, to);
        put_piece(promoted, to);
    }
    else if (flag == Move::PAWN_TWO_UP_FLAG)
    {
        next_state.ep_square = (us == Color::WHITE) ? to - 8 : to + 8;
    }

    next_state.castling_rights &= CASTLING_UPDATE[from];
    next_state.castling_rights &= CASTLING_UPDATE[to];

    side_to_move = !side_to_move;
    ply++;
}

void Board::unmake_move(Move mv) noexcept
{
    [[assume(ply > 0)]];

    ply--;
    side_to_move = !side_to_move;

    const Square from = mv.get_start_square();
    const Square to = mv.get_target_square();
    const u16 flag = mv.get_flag();

    const State& state = history[ply];
    const Piece moved = state.moved_piece;
    const Piece captured = state.captured_piece;
    const Color us = side_to_move;

    if (mv.is_promotion())
    {
        remove_piece(pieces[to], to);
        put_piece(moved, to);
    }

    move_piece(moved, to, from);

    if (flag == Move::ENPASSANT_CAPTURE_FLAG)
    {
        Square capture_sq = (us == Color::WHITE) ? to - 8 : to + 8;
        put_piece(captured, capture_sq);
    }
    else if (captured != EMPTY)
    {
        put_piece(captured, to);
    }

    if (flag == Move::CASTLE_FLAG)
    {
        const Piece rook = make_piece(Type::ROOK, us);
        switch (to)
        {
            case 6:
                move_piece(rook, 5, 7);
                break; // White Kingside
            case 2:
                move_piece(rook, 3, 0);
                break; // White Queenside
            case 62:
                move_piece(rook, 61, 63);
                break; // Black Kingside
            case 58:
                move_piece(rook, 59, 56);
                break; // Black Queenside
        }
    }
}

Board Board::from_startpos() { return from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").value(); }

std::expected<Board, std::string> Board::from_fen(std::string fen)
{
    Board board;

    std::istringstream iss(fen);
    std::vector<std::string> parts;
    std::string part;
    while (iss >> part)
        parts.push_back(part);

    if (parts.size() < 4)
    {
        return std::unexpected("FEN string is incomplete.");
    }

    std::string layout = parts[0];
    std::string active = parts[1];
    std::string castling = parts[2];
    std::string ep = parts[3];
    std::string halfmove = parts.size() > 4 ? parts[4] : "0";

    int rank = 7;
    int file = 0;

    for (char ch : layout)
    {
        if (ch == '/')
        {
            if (file != 8)
                return std::unexpected(std::format("Rank {} has {} squares instead of 8.", rank + 1, file));
            rank--;
            file = 0;
            if (rank < 0)
                return std::unexpected("Too many ranks provided in FEN layout.");
        }
        else if (ch >= '1' && ch <= '8')
        {
            file += (ch - '0');
            if (file > 8)
                return std::unexpected(std::format("Too many squares on rank {}.", rank + 1));
        }
        else
        {
            if (file >= 8)
                return std::unexpected(std::format("Too many squares on rank {}.", rank + 1));

            Type type;
            Color color = Color::BLACK;
            switch (ch)
            {
                case 'p':
                    type = Type::PAWN;
                    break;
                case 'n':
                    type = Type::KNIGHT;
                    break;
                case 'b':
                    type = Type::BISHOP;
                    break;
                case 'r':
                    type = Type::ROOK;
                    break;
                case 'q':
                    type = Type::QUEEN;
                    break;
                case 'k':
                    type = Type::KING;
                    break;
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
                default:
                    return std::unexpected(std::format("Invalid piece character '{}'.", ch));
            }

            Square sq = static_cast<Square>(rank * 8 + file);
            board.put_piece(make_piece(type, color), sq);
            file++;
        }
    }

    if (rank != 0 || file != 8)
        return std::unexpected("Incomplete FEN board layout.");

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
                case 'K':
                    rights |= CastlingRights::WK;
                    break;
                case 'Q':
                    rights |= CastlingRights::WQ;
                    break;
                case 'k':
                    rights |= CastlingRights::BK;
                    break;
                case 'q':
                    rights |= CastlingRights::BQ;
                    break;
                default:
                    return std::unexpected(std::format("Invalid castling right character '{}'.", c));
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
        if (ep.length() != 2)
            return std::unexpected("Invalid en passant square format.");
        char f = ep[0];
        char r = ep[1];
        if (f < 'a' || f > 'h' || r < '1' || r > '8')
            return std::unexpected("Invalid en passant square coordinates.");

        board.history[0].ep_square = static_cast<Square>((r - '1') * 8 + (f - 'a'));
    }

    u16 clock_val = 0;
    auto [ptr, ec] = std::from_chars(halfmove.data(), halfmove.data() + halfmove.size(), clock_val);

    if (ec != std::errc{} || ptr != halfmove.data() + halfmove.size())
    {
        return std::unexpected("Invalid halfmove clock provided in FEN string.");
    }

    board.history[0].halfmove_clock = clock_val;

    return board;
}

void Board::display(bool white_perspective) const
{
    std::cout << "  +-----------------+\n";

    static constexpr std::array<int, 8> NUMS_IN_ORDER = {0, 1, 2, 3, 4, 5, 6, 7};
    static constexpr std::array<int, 8> NUMS_IN_REVERSE = {7, 6, 5, 4, 3, 2, 1, 0};

    const auto& ranks = white_perspective ? NUMS_IN_REVERSE : NUMS_IN_ORDER;
    const auto& files = white_perspective ? NUMS_IN_ORDER : NUMS_IN_REVERSE;

    for (int rank : ranks)
    {
        std::cout << rank + 1 << " | ";
        for (int file : files)
        {
            Square sq = static_cast<Square>(rank * 8 + file);
            Piece piece = pieces[sq];
            char ch = piece_to_char(piece);
            std::cout << ch << " ";
        }
        std::cout << "|\n";
    }

    std::cout << "  +-----------------+\n";

    if (white_perspective)
    {
        std::cout << "    a b c d e f g h\n";
    }
    else
    {
        std::cout << "    h g f e d c b a\n";
    }
}