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

    constexpr usize N = eve::expected_cardinal_v<i32>;
    using wide_i16_N = eve::wide<i16, eve::fixed<N>>;
    using wide_i32_N = eve::wide<i32, eve::fixed<N>>;

    const auto& net = get_network();

    const i16* us = (side_to_move == Color::WHITE) ? acc.white : acc.black;
    const i16* them = (side_to_move == Color::WHITE) ? acc.black : acc.white;

    ALIGN i16 activated[512];
    auto zero = wide<i16>(0);
    auto qa = wide<i16>(nnue_constants::QA);

    constexpr usize N16 = eve::expected_cardinal_v<i16>;
    for (usize i = 0; i < nnue_constants::HIDDEN; i += N16)
    {
        auto us_val = eve::clamp(eve::load(us + i), zero, qa);
        auto them_val = eve::clamp(eve::load(them + i), zero, qa);
        eve::store(us_val, activated + i);
        eve::store(them_val, activated + nnue_constants::HIDDEN + i);
    }

    // Layer 1 (512 -> 32).
    ALIGN i16 l1_out[32];
    for (usize i = 0; i < 32; i += 8)
    {
        wide_i32_N sum0(0), sum1(0), sum2(0), sum3(0), sum4(0), sum5(0), sum6(0), sum7(0);

        for (usize j = 0; j < 512; j += N)
        {
            auto act = eve::convert(eve::load(activated + j, as<wide_i16_N> {}), as<i32>());
            auto w0 = eve::convert(eve::load(net.l1_weights[i + 0] + j, as<wide_i16_N> {}), as<i32>());
            auto w1 = eve::convert(eve::load(net.l1_weights[i + 1] + j, as<wide_i16_N> {}), as<i32>());
            auto w2 = eve::convert(eve::load(net.l1_weights[i + 2] + j, as<wide_i16_N> {}), as<i32>());
            auto w3 = eve::convert(eve::load(net.l1_weights[i + 3] + j, as<wide_i16_N> {}), as<i32>());
            auto w4 = eve::convert(eve::load(net.l1_weights[i + 4] + j, as<wide_i16_N> {}), as<i32>());
            auto w5 = eve::convert(eve::load(net.l1_weights[i + 5] + j, as<wide_i16_N> {}), as<i32>());
            auto w6 = eve::convert(eve::load(net.l1_weights[i + 6] + j, as<wide_i16_N> {}), as<i32>());
            auto w7 = eve::convert(eve::load(net.l1_weights[i + 7] + j, as<wide_i16_N> {}), as<i32>());

            sum0 = eve::fma(act, w0, sum0);
            sum1 = eve::fma(act, w1, sum1);
            sum2 = eve::fma(act, w2, sum2);
            sum3 = eve::fma(act, w3, sum3);
            sum4 = eve::fma(act, w4, sum4);
            sum5 = eve::fma(act, w5, sum5);
            sum6 = eve::fma(act, w6, sum6);
            sum7 = eve::fma(act, w7, sum7);
        }

        auto finish_l1 = [&](const wide_i32_N& s, usize idx) {
            i32 bias = static_cast<i32>(net.l1_biases[idx]) * nnue_constants::QA;
            i32 sum = eve::reduce(s) + bias;
            l1_out[idx] =
                static_cast<i16>(std::clamp(sum / nnue_constants::QA, 0, static_cast<i32>(nnue_constants::QB)));
        };

        finish_l1(sum0, i + 0);
        finish_l1(sum1, i + 1);
        finish_l1(sum2, i + 2);
        finish_l1(sum3, i + 3);
        finish_l1(sum4, i + 4);
        finish_l1(sum5, i + 5);
        finish_l1(sum6, i + 6);
        finish_l1(sum7, i + 7);
    }

    // Layer 2 (32 -> 32).
    ALIGN i16 l2_out[32];
    for (usize i = 0; i < 32; i += 4)
    {
        wide_i32_N sum0(0), sum1(0), sum2(0), sum3(0);
        for (usize j = 0; j < 32; j += N)
        {
            auto act = eve::convert(eve::load(l1_out + j, as<wide_i16_N> {}), as<i32>());
            auto w0 = eve::convert(eve::load(net.l2_weights[i + 0] + j, as<wide_i16_N> {}), as<i32>());
            auto w1 = eve::convert(eve::load(net.l2_weights[i + 1] + j, as<wide_i16_N> {}), as<i32>());
            auto w2 = eve::convert(eve::load(net.l2_weights[i + 2] + j, as<wide_i16_N> {}), as<i32>());
            auto w3 = eve::convert(eve::load(net.l2_weights[i + 3] + j, as<wide_i16_N> {}), as<i32>());

            sum0 = eve::fma(act, w0, sum0);
            sum1 = eve::fma(act, w1, sum1);
            sum2 = eve::fma(act, w2, sum2);
            sum3 = eve::fma(act, w3, sum3);
        }

        auto finish_l2 = [&](const wide_i32_N& s, usize idx) {
            i32 bias = static_cast<i32>(net.l2_biases[idx]) * nnue_constants::QB;
            i32 res = eve::reduce(s) + bias;
            l2_out[idx] =
                static_cast<i16>(std::clamp(res / nnue_constants::QB, 0, static_cast<i32>(nnue_constants::QB)));
        };

        finish_l2(sum0, i + 0);
        finish_l2(sum1, i + 1);
        finish_l2(sum2, i + 2);
        finish_l2(sum3, i + 3);
    }

    // Layer 3 (32 -> 1).
    wide_i32_N sum_l3(0);
    for (usize j = 0; j < 32; j += N)
    {
        auto act = eve::convert(eve::load(l2_out + j, as<wide_i16_N> {}), as<i32>());
        auto w = eve::convert(eve::load(net.l3_weights[0] + j, as<wide_i16_N> {}), as<i32>());
        sum_l3 = eve::fma(act, w, sum_l3);
    }
    i32 l3_bias = static_cast<i32>(net.l3_biases[0]) * nnue_constants::QB;
    i32 final_sum = eve::reduce(sum_l3) + l3_bias;

    // Output descaling.
    constexpr i64 DIVISOR = static_cast<i64>(nnue_constants::QA) * nnue_constants::QB * nnue_constants::QB;

    Score eval = static_cast<Score>((static_cast<i64>(final_sum) * nnue_constants::SCALE) / DIVISOR);

    return std::clamp(eval, static_cast<Score>(-MATE_THRESHOLD + 1), static_cast<Score>(MATE_THRESHOLD - 1));
}