# Philidor

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg?style=flat-square&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/23)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg?style=flat-square)](LICENSE)
[![Estimated Elo](https://img.shields.io/badge/Estimated%20Elo-2850--2900-orange.svg?style=flat-square)](#strength--elo-calibration)

**Philidor** is an open-source, UCI-compliant chess engine written from scratch in modern **C++23** by **Antoine Lequeux**. Currently in its early phases of active development, Philidor operates at an estimated strength between **2850 and 2900 Elo**, calibrated through fast tournament matches (5+0.2 blitz with opening books) against calibrated Stockfish levels.

---

## Technical Summary

Philidor required understanding low-level systems engineering, modern C++ paradigms, and advanced algorithmic optimization:

- **Modern Systems C++ (C++23)**: Leverages ISO C++23 features including `#embed` for zero-overhead asset compilation, `<bit>` intrinsics, standard ranges, and compile-time `constexpr` evaluation with strict zero-warning compilation (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`).
- **High-Performance SIMD Computing**: Employs [EVE (Expressive Vector Engine)](https://github.com/jfalcou/eve) for portable, target-agnostic SIMD vectorization across AVX2, AVX-512, and ARM NEON, achieving **~1.44 million neural network evaluations/second** on a single thread.
- **Deep Learning / Neural Evaluation (NNUE)**: Custom quantized $768 \to 256 \times 2 \to 32 \to 32 \to 1$ neural network trained on **~350 million Stockfish-evaluated positions**, with dual-perspective differential accumulators and lazy evaluation.
- **Advanced Graph Search & Optimization**: Highly selective alpha-beta search with Principal Variation Search (PVS), adaptive aspiration windows, deep forward pruning (NMP, RFP, ProbCut, Futility, LMP, SEE), singular extensions with multi-cut, and a 7-stage heuristic move ordering pipeline.
- **High-Throughput Bitboard Engine**: Plain Magic Bitboards for slider ray generation, custom branchless bit-iterators, and a cacheline-aligned 16-byte Transposition Table yielding **>220 nodes/sec** in perft.
- **Automated Hyperparameter Optimization (MLOps)**: Macro-driven parameter definitions with automated Optuna JSON search space generation, enabling distributed Bayesian parameter tuning via UCI pipes.

---

## Neural Network Evaluation (NNUE) & SIMD

Philidor replaces hand-crafted heuristics with a custom, highly quantized Neural Network Evaluation (NNUE) architecture implemented in [`src/nnue.hpp`](src/nnue.hpp) and [`src/nnue.cpp`](src/nnue.cpp).

```
Input Features (768: 12 piece types x 64 squares)
       │
       ├──[ White POV ]───> Linear (768 -> 256) ──> [ Accumulator White: 256 x i16 ]
       └──[ Black POV ]───> Linear (768 -> 256) ──> [ Accumulator Black: 256 x i16 ]
                                                          │
                                         [ Concatenate Perspective: 512 x i16 ]
                                                          │
                                               ClippedReLU(0, QA=255)
                                                          │
                                            Layer 1: Linear (512 -> 32)
                                                          │
                                                ClippedReLU(0, QB=64)
                                                          │
                                            Layer 2: Linear (32 -> 32)
                                                          │
                                                ClippedReLU(0, QB=64)
                                                          │
                                            Layer 3: Linear (32 -> 1)
                                                          │
                                              Output Descaling (SCALE = 400)
                                                          │
                                                 Evaluation (Score cp)
```

- **Topology & Quantization**: A dual-perspective HalfKP feature transformer ($768 \to 256 \times 2$) feeding a 3-layer MLP ($512 \to 32 \to 32 \to 1$). Weights and activations are quantized to 16-bit signed integers (`i16`) with clipped ReLU bounds ($QA = 255$, $QB = 64$) ([`src/nnue.hpp:11-15`](src/nnue.hpp#L11-L15)). The entire network is exactly **428.8 KB**, residing comfortably in L2/L3 cache.
- **Dataset & Training**: Trained on **~350 million Stockfish-evaluated positions** with integer-aware quantization-aware training (QAT) across master-level and self-play opening/middlegame/endgame distributions.
- **Target-Agnostic SIMD with EVE**: Evaluated via [EVE](https://github.com/jfalcou/eve) in [`src/nnue.cpp`](src/nnue.cpp). Unrolled fused multiply-accumulate pipelines (`eve::fma`) and vector clamps (`eve::clamp`) compile natively to AVX2, AVX-512, or ARM NEON with zero platform-specific assembly, achieving **1.44M evals/sec**.
- **Differential Accumulators & Lazy Updates**: When moves are made, [`NNUE::move_piece`](src/nnue.cpp#L101-L126) updates White and Black accumulators incrementally in one vectorized sweep. Accumulator recalculation is deferred through [`Board::ensure_accumulator()`](src/board.cpp#L535): branches pruned prior to leaf evaluation incur **zero evaluation overhead**.
- **C++23 `#embed`**: The network binary is embedded directly into the executable via `#embed "network.nnue"` ([`src/nnue.cpp:11-13`](src/nnue.cpp#L11-L13)), removing any external file dependency.

---

## Search Architecture & Tree Pruning

Philidor executes an aggressive, highly selective alpha-beta minimax search centered around **Principal Variation Search (PVS)** ([`src/search.cpp`](src/search.cpp), [`src/search.hpp`](src/search.hpp)):

- **PVS & Dynamic Aspiration Windows**: Searches the principal variation move with a full $(\alpha, \beta)$ window and sibling moves with zero-width null windows ($-\alpha - 1, -\alpha$) ([`src/search.cpp:1185-1196`](src/search.cpp#L1185-L1196)). Aspiration windows dynamically widen exponentially around the prior iteration's score upon fails low/high ([`src/search.hpp:256-297`](src/search.hpp#L256-L297)).
- **Selective Pruning Suite**:
  - **Null Move Pruning (NMP)**: Dynamic reduction $R = R_{\text{base}} + \lfloor \text{depth}/4 \rfloor + \min(3, \lfloor (\text{eval} - \beta)/200 \rfloor)$, guarded against zugzwang ([`src/search.cpp:845-885`](src/search.cpp#L845-L885)).
  - **Reverse Futility Pruning (RFP / Static Null Move)**: Prunes at depth $\le 7$ when $static\_eval - margin \ge \beta$ with an improving-node bonus ([`src/search.cpp:795-802`](src/search.cpp#L795-L802)).
  - **ProbCut**: Speculative tactical cutoffs on captures tested with Static Exchange Evaluation (SEE) verification ([`src/search.cpp:805-832`](src/search.cpp#L805-L832)).
  - **Futility & Late Move Pruning (FP / LMP)**: Skips low-potential quiet moves at shallow depths and cuts off quiet moves after a depth-dependent move threshold ([`src/search.cpp:910-955`](src/search.cpp#L910-L955)).
  - **History & SEE Pruning**: Prunes quiet moves with negative butterfly history and captures with losing exchanges ($SEE < -50\text{ cp}$) ([`src/search.cpp:935-975`](src/search.cpp#L935-L975)).
- **Reductions & Extensions**:
  - **Late Move Reductions (LMR)**: 2D logarithmic reduction table indexed by `[depth][move_count]`, modulated by PV node status, improving context, and continuation history ([`src/search.cpp:1005-1065`](src/search.cpp#L1005-L1065)).
  - **Singular Extensions (SE) & Multi-Cut**: Proves whether the TT move uniquely dominates alternatives by searching alternatives with reduced depth; grants $+1$ (or $+2$) extensions if singular, or triggers a **Multi-Cut** prune if an alternative refutes it ([`src/search.cpp:985-1035`](src/search.cpp#L985-L1035)).
  - **Check & Passed Pawn Extensions**: Extends tactical checks and passed pawns advancing to the 7th rank ([`src/search.cpp:975-985`](src/search.cpp#L975-L985)).
- **Quiescence Search**: Evaluates tactical sequences with stand-pat, MVV-LVA capture ordering, delta pruning with queen margins, and negative-SEE pruning ([`src/search.cpp:615-724`](src/search.cpp#L615-L724)).
- **7-Stage Heuristic Move Ordering**: Driven by a staged [`MovePicker`](src/search.cpp#L170-L450):
  1. TT Hash Move
  2. Good Captures (MVV-LVA / SEE $\ge 0$)
  3. Killer Moves (2 slots/ply)
  4. Countermoves (`countermoves[piece][to]`)
  5. Continuation History (1-ply & 2-ply tables)
  6. Butterfly History (`history[color][from_to]`)
  7. Bad Captures ($SEE < 0$)
- **Evaluation Correction History**: Dynamically tracks and corrects static evaluation drift based on pawn-structure hashes (`correction_history[color][pawn_hash % 16384]`) ([`src/search.cpp:605-613`](src/search.cpp#L605-L613)).

---

## Transposition Table (TT)

Philidor implements a cacheline-friendly 2-tier Transposition Table in [`src/tt.hpp`](src/tt.hpp) and [`src/tt.cpp`](src/tt.cpp):

```cpp
struct TTEntry
{
    u64 key;          // 8 bytes: 64-bit Zobrist key
    Move move;        // 2 bytes: Best move
    i16 score;        // 2 bytes: Ply-normalized minimax score
    i8 depth;         // 1 byte:  Search depth
    u8 bound_age;     // 1 byte:  Bound (bits 0:1) & Age (bits 2:7)
    i16 static_eval;  // 2 bytes: Static evaluation score
};
static_assert(sizeof(TTEntry) == 16);
```

- **Exact 16-Byte Packing**: Exactly four entries fit within a standard 64-byte CPU cache line with zero false sharing or memory padding.
- **Static Eval Caching**: Retains `static_eval` directly in the TT entry, bypassing NNUE calls on subsequent visits or bounds cutoffs.
- **2-Tier Replacement**: Balances search generation aging (`current_age`) with depth prioritization to preserve deep variations while refreshing stale entries ([`src/tt.cpp:40-76`](src/tt.cpp#L40-L76)).

---

## Move Generation & Bitboards

- **Plain Magic Bitboards**: Sliding ray attacks for rooks and bishops use single multiplication and shift hash lookups over precomputed attack databases ([`src/magic.hpp`](src/magic.hpp), [`src/magic.cpp`](src/magic.cpp)).
- **Branchless Iteration**: Custom `Bitloop` iterator leverages `std::countr_zero` hardware instructions for zero-overhead bitboard traversal ([`src/defines.hpp:237-256`](src/defines.hpp#L237-L256)).
- **Staged Move Generation**: Pseudo-legal moves are generated on-demand (`GenType::CAPTURES`, `GenType::QUIETS`, `GenType::ALL`) with pinned-piece masks ([`src/movegen.hpp`](src/movegen.hpp)).
- **Performance**: Move generation throughput exceeds **220 million nodes/second** single-threaded (`perft 5` in **22 ms**).

---

## Automated Tuning Infrastructure (Optuna / SPSA)

Every critical search threshold, margin, and reduction coefficient is declared via the `TUNABLE_PARAM` macro ([`src/search.hpp:42-125`](src/search.hpp#L42-L125)):

- **Zero Production Cost**: In release builds (`-DTUNE_BUILD=OFF`), parameters compile into static `constexpr` values with zero indirection overhead.
- **UCI Parameter Exposure**: When built with `-DTUNE_BUILD=ON`, every parameter registers into a central registry and is exposed via standard UCI spin options ([`src/uci.cpp:108-114`](src/uci.cpp#L108-L114)).
- **Optuna JSON Export**: Sending the `json` UCI command executes [`Params::print_optuna_json()`](src/search.hpp#L127-L142), outputting an Optuna-ready parameter dictionary for automated Bayesian hyperparameter tuning.

---

## Building & Running

### Prerequisites
- C++23 compiler: **Clang 19+** (recommended), **GCC 14+**, or **MSVC 19.38+**
- **CMake 3.20+** & **Ninja**

### Compilation

```bash
# Standard Release Build
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target philidor

# Optional: Tuning Build for Optuna / SPSA
cmake -B build/tune -G Ninja -DCMAKE_BUILD_TYPE=Release -DTUNE_BUILD=ON
cmake --build build/tune --target philidor
```

### UCI Usage

Philidor works with any modern chess GUI (Cutechess, BanksiaGUI, Arena, Nibbler, En Croissant):

```bash
./build/release/philidor
uci
isready
position startpos
go depth 15
```

---

## License

Philidor is open-source software licensed under the **GNU General Public License v3.0 (GPLv3)**. See [`LICENSE`](LICENSE) for details.
