#include "nnue.hpp"
#include "board.hpp"

#include <cstring>
#include <eve/eve.hpp>
#include <eve/module/core.hpp>

#if defined(__clang__)
    #pragma clang diagnostic ignored "-Wc23-extensions"
#endif
ALIGN constexpr u8 EMBEDDED_NNUE_DATA[] = {
#embed "network.nnue"
};

inline RuntimeNetworkWeights g_network;

void NNUE::init()
{
    const FileNetworkWeights* file_net = reinterpret_cast<const FileNetworkWeights*>(EMBEDDED_NNUE_DATA);

    std::memcpy(g_network.feature_weights, file_net->feature_weights, sizeof(g_network.feature_weights));
    std::copy(
        std::begin(file_net->feature_biases), std::end(file_net->feature_biases), std::begin(g_network.feature_biases)
    );

    for (usize in = 0; in < 512; ++in)
        for (usize out = 0; out < 32; ++out) g_network.l1_weights[out][in] = file_net->l1_weights[in][out];
    std::memcpy(g_network.l1_biases, file_net->l1_biases, sizeof(g_network.l1_biases));

    for (usize in = 0; in < 32; ++in)
        for (usize out = 0; out < 32; ++out) g_network.l2_weights[out][in] = file_net->l2_weights[in][out];
    std::memcpy(g_network.l2_biases, file_net->l2_biases, sizeof(g_network.l2_biases));

    for (usize in = 0; in < 32; ++in) g_network.l3_weights[0][in] = file_net->l3_weights[in][0];
    std::memcpy(g_network.l3_biases, file_net->l3_biases, sizeof(g_network.l3_biases));
}

[[nodiscard]] inline const RuntimeNetworkWeights& get_network()
{
    return g_network;
}

constexpr usize get_feature_index(usize color, usize piece_type, Square sq, bool is_black_pov)
{
    if (is_black_pov)
    {
        sq ^= 56;
        color ^= 1;
    }

    return (color * 6 + piece_type) * 64 + static_cast<usize>(sq);
}

void Accumulator::refresh()
{
    const auto& net = get_network();
    std::copy(std::begin(net.feature_biases), std::end(net.feature_biases), white);
    std::copy(std::begin(net.feature_biases), std::end(net.feature_biases), black);
}

void NNUE::add_piece(Accumulator& acc, usize color, usize piece_type, Square sq)
{
    const auto& net = get_network();
    usize w_idx = get_feature_index(color, piece_type, sq, false);
    usize b_idx = get_feature_index(color, piece_type, sq, true);

    using eve::wide;
    constexpr usize N = eve::expected_cardinal_v<i16>;

    for (usize i = 0; i < nnue_constants::HIDDEN; i += N)
    {
        auto w_acc = eve::load(acc.white + i);
        auto w_wgt = eve::load(net.feature_weights[w_idx] + i);
        eve::store(w_acc + w_wgt, acc.white + i);

        auto b_acc = eve::load(acc.black + i);
        auto b_wgt = eve::load(net.feature_weights[b_idx] + i);
        eve::store(b_acc + b_wgt, acc.black + i);
    }
}

void NNUE::remove_piece(Accumulator& acc, usize color, usize piece_type, Square sq)
{
    const auto& net = get_network();
    usize w_idx = get_feature_index(color, piece_type, sq, false);
    usize b_idx = get_feature_index(color, piece_type, sq, true);

    using eve::wide;
    constexpr usize N = eve::expected_cardinal_v<i16>;

    for (usize i = 0; i < nnue_constants::HIDDEN; i += N)
    {
        auto w_acc = eve::load(acc.white + i);
        auto w_wgt = eve::load(net.feature_weights[w_idx] + i);
        eve::store(w_acc - w_wgt, acc.white + i);

        auto b_acc = eve::load(acc.black + i);
        auto b_wgt = eve::load(net.feature_weights[b_idx] + i);
        eve::store(b_acc - b_wgt, acc.black + i);
    }
}

void NNUE::move_piece(Accumulator& acc, usize color, usize piece_type, Square from_sq, Square to_sq)
{
    const auto& net = get_network();

    usize from_w_idx = get_feature_index(color, piece_type, from_sq, false);
    usize from_b_idx = get_feature_index(color, piece_type, from_sq, true);
    usize to_w_idx = get_feature_index(color, piece_type, to_sq, false);
    usize to_b_idx = get_feature_index(color, piece_type, to_sq, true);

    using eve::wide;
    constexpr usize N = eve::expected_cardinal_v<i16>;

    for (usize i = 0; i < nnue_constants::HIDDEN; i += N)
    {
        // White POV
        auto w_acc = eve::load(acc.white + i);
        w_acc -= eve::load(net.feature_weights[from_w_idx] + i);
        w_acc += eve::load(net.feature_weights[to_w_idx] + i);
        eve::store(w_acc, acc.white + i);

        // Black POV
        auto b_acc = eve::load(acc.black + i);
        b_acc -= eve::load(net.feature_weights[from_b_idx] + i);
        b_acc += eve::load(net.feature_weights[to_b_idx] + i);
        eve::store(b_acc, acc.black + i);
    }
}

void NNUE::update_full(Accumulator& acc, const Board& board)
{
    acc.refresh();
    for (Square sq = 0; sq < 64; ++sq)
    {
        Piece p = board.pieces[sq];
        if (p != EMPTY) add_piece(acc, color_index(get_piece_color(p)), piece_type_index(get_piece_type(p)), sq);
    }
}

Score NNUE::evaluate(const Accumulator& acc, Color side_to_move)
{
    using eve::as;
    using eve::wide;

    constexpr usize N = eve::expected_cardinal_v<i16>;

    using wide_i32_N = eve::wide<i32, eve::fixed<N>>;

    const auto& net = get_network();

    const i16* us = (side_to_move == Color::WHITE) ? acc.white : acc.black;
    const i16* them = (side_to_move == Color::WHITE) ? acc.black : acc.white;

    // Accumulator activation (clipped ReLU).
    ALIGN i16 activated[512];
    auto zero = wide<i16>(0);
    auto qa = wide<i16>(nnue_constants::QA);

    for (usize i = 0; i < nnue_constants::HIDDEN; i += N)
    {
        auto us_val = eve::clamp(eve::load(us + i), zero, qa);
        auto them_val = eve::clamp(eve::load(them + i), zero, qa);
        eve::store(us_val, activated + i);
        eve::store(them_val, activated + nnue_constants::HIDDEN + i);
    }

    // Layer 1 (512 -> 32).
    ALIGN i16 l1_out[32];
    for (usize i = 0; i < 32; ++i)
    {
        wide_i32_N sum_vec(0);

        for (usize j = 0; j < 512; j += N)
        {
            auto act = eve::load(activated + j);
            auto wgt = eve::load(net.l1_weights[i] + j);
            sum_vec += eve::convert(act, as<i32>()) * eve::convert(wgt, as<i32>());
        }

        i32 bias = static_cast<i32>(net.l1_biases[i]) * nnue_constants::QA;
        i32 sum = eve::reduce(sum_vec) + bias;

        l1_out[i] = static_cast<i16>(std::clamp(sum / nnue_constants::QA, 0, static_cast<i32>(nnue_constants::QB)));
    }

    // Layer 2 (32 -> 32).
    ALIGN i16 l2_out[32];
    for (usize i = 0; i < 32; ++i)
    {
        wide_i32_N sum_vec(0);

        for (usize j = 0; j < 32; j += N)
        {
            auto act = eve::load(l1_out + j);
            auto wgt = eve::load(net.l2_weights[i] + j);
            sum_vec += eve::convert(act, as<i32>()) * eve::convert(wgt, as<i32>());
        }

        i32 bias = static_cast<i32>(net.l2_biases[i]) * nnue_constants::QB;
        i32 sum = eve::reduce(sum_vec) + bias;

        l2_out[i] = static_cast<i16>(std::clamp(sum / nnue_constants::QB, 0, static_cast<i32>(nnue_constants::QB)));
    }

    // Layer 3 (32 -> 1).
    wide_i32_N final_sum_vec(0);
    for (usize j = 0; j < 32; j += N)
    {
        auto act = eve::load(l2_out + j);
        auto wgt = eve::load(net.l3_weights[0] + j);
        final_sum_vec += eve::convert(act, as<i32>()) * eve::convert(wgt, as<i32>());
    }

    i32 l3_bias = static_cast<i32>(net.l3_biases[0]) * nnue_constants::QB;
    i32 final_sum = eve::reduce(final_sum_vec) + l3_bias;

    // Output descaling.
    constexpr i64 DIVISOR = static_cast<i64>(nnue_constants::QA) * nnue_constants::QB * nnue_constants::QB;

    Score eval = static_cast<Score>((static_cast<i64>(final_sum) * nnue_constants::SCALE) / DIVISOR);

    return std::clamp(eval, static_cast<Score>(-MATE_THRESHOLD + 1), static_cast<Score>(MATE_THRESHOLD - 1));
}