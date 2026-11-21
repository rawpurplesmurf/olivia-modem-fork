# olivia-modem (`olivia-modem-fork`)

A standalone, zero-dependency C++20 **Olivia MFSK** weak-signal HF modem library extracted from [fldigi](https://sourceforge.net/projects/fldigi/) and packaged as a clean static library (`liboliviamodem.a`) with a minimal PIMPL C++ interface.

`olivia-modem-fork` is a maintained fork of Jeff Francis (N0GQ)'s [`jfrancis42/olivia-modem`](https://github.com/jfrancis42/olivia-modem). It wraps Pawel Jalocha's (SP9VRC) battle-tested `MFSK_Transmitter` and `MFSK_Receiver` DSP templates without pulling in FLTK, `libsamplerate`, `libsndfile`, or fldigi global state—making it straightforward to embed in headless daemons, SDR transceivers, command-line tools, and desktop radio applications such as [`jf8call-fork`](../jf8call-fork).

---

## About This Fork

This repository is forked from [`jfrancis42/olivia-modem`](https://github.com/jfrancis42/olivia-modem) to serve as a clean, documented, CI-verified companion modem library alongside [`jf8call-fork`](../jf8call-fork), [`gfsk8-modem-clean-fork`](../gfsk8-modem-clean-fork), and [`libpsk-fork`](../libpsk-fork). Improvements and cleanup in this fork include:

- **Repository Hygiene:** Removed tracked build artifacts from Git history and added `.gitignore` so builds stay cleanly out-of-tree.
- **Complete Technical Documentation:** Added a thorough documentation suite covering the public API, DSP internals and verified mode math (correcting throughput/WPM figures against `pj_mfsk.h`), behavioral caveats, audio pipeline integration, and C++20 loopback/streaming recipes.
- **Maintenance & CI Tooling:** Added [`CHANGELOG.md`](CHANGELOG.md), [`.markdownlint-cli2.jsonc`](.markdownlint-cli2.jsonc) for automated documentation linting, and isolated branch/PR workflow guidelines ([`AGENTS.md`](AGENTS.md)).
- **Strict Zero-Dependency Parity:** Preserves wire-level interoperability with fldigi and upstream `jfrancis42/olivia-modem` while keeping dependencies strictly limited to C++20 and `libm`.

---

## Documentation

| Document | Audience & Contents |
| --- | --- |
| **[API Reference](docs/api-reference.md)** | Complete reference for `OliviaConfig`, `OliviaLib`, TX/RX semantics, metrics, thread safety, error handling, and known behaviors. |
| **[DSP Architecture & Protocol](docs/dsp-architecture.md)** | Deep dive into the Olivia waveform, Walsh–Hadamard FEC, interleaving, MFSK modulation, spectral QRM suppression, multi-hypothesis sync, mode math, and `include/jalocha/*.h` inventory. |
| **[Integration & Audio Pipeline Guide](docs/integration-guide.md)** | CMake and standalone build integration, 48 kHz-to-8 kHz resampling, RX carrier tuning, complete loopback and thread-safe streaming examples, channel presets, and troubleshooting. |
| **[Contributing & Maintenance](docs/contributing.md)** | Upstream provenance, fork workflow, PR/CI gates, zero-dependency rules, linting, and testing procedures. |
| **[Changelog](CHANGELOG.md)** | Version and fork history. |

---

## Key Features

- **Interoperable Olivia MFSK** — Wire-compatible with fldigi and other Olivia implementations across standard tone/bandwidth combinations (4–64 tones, 125–2000 Hz bandwidth; underlying templates support up to 256 tones).
- **Walsh–Hadamard FEC + Time/Frequency Interleaving** — Each 7-bit ASCII character is encoded into a 64-symbol orthogonal Hadamard codeword, scrambled, and diagonally interleaved across tone bit planes for strong resistance to HF multipath fading (QSB) and narrowband interference (QRM).
- **Built-In Interference Suppression** — Receiver front-end applies overlapping STFT spectral peak limiting (narrowband carrier/QRM blanking), spectral whitening, and time-domain impulse clipping before demodulation.
- **Zero External Dependencies** — Requires only a C++20 compiler, the C++ standard library, and `libm`. No FLTK, no `libsamplerate`, no `libsndfile`, no global mutable state, and no internal threads.
- **Simple Audio Contract** — All TX output and RX input is mono **8000 Hz** IEEE-754 `double` PCM in `[-1.0, +1.0]`.
- **Clean ABI Boundary** — `include/oliviamodem.h` exposes only standard library types and hides the Jalocha template headers behind a `std::unique_ptr<Impl>` PIMPL.
- **Portable** — Builds cleanly on Linux x86-64, Raspberry Pi (`aarch64`/`armv7`), and any POSIX system with CMake 3.16+ and a C++20 compiler.

---

## Requirements

- **CMake** 3.16 or newer
- **C++20 compiler** (GCC 10+, Clang 11+, or equivalent)
- **Standard math library** (`libm`, linked automatically via CMake)

---

## Quick Start

### Building the Static Library

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces `build/liboliviamodem.a`. To install the library and headers into a prefix (for example `/usr/local`):

```bash
cmake --install build --prefix /usr/local
```

### Linking in CMake

Include `olivia-modem` directly as a subdirectory:

```cmake
add_subdirectory(path/to/olivia-modem-fork)
target_link_libraries(my_app PRIVATE oliviamodem)
```

Or link a prebuilt `liboliviamodem.a` as an `IMPORTED` static target (the pattern used by `jf8call-fork`; see [Integration Guide](docs/integration-guide.md#integrating-with-cmake) for details).

### Minimal TX/RX Loopback Example

```cpp
#include "oliviamodem.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

int main() {
    // Configure Olivia 8/500 (8 tones, 500 Hz bandwidth)
    OliviaConfig cfg;
    cfg.tones        = 8;     // 4, 8, 16, 32, or 64
    cfg.bandwidthHz  = 500;   // 125, 250, 500, 1000, or 2000 Hz
    cfg.syncMargin   = 8;     // frequency search half-width in FFT bins
    cfg.syncIntegLen = 4;     // synchronizer integration length (FEC blocks)
    cfg.squelch      = 0.0;   // SNR threshold (receiver enforces >= 3.0)
    cfg.sendTones    = true;  // prepend synchronization preamble tones

    OliviaLib modem(cfg);

    // 1. TX: modulate text at 1500 Hz carrier (8000 Hz double PCM in [-1, +1])
    std::vector<double> audio = modem.modulate("CQ CQ DE N0GQ K\n", 1500.0);

    // 2. RX: stream audio into the decoder in 512-sample chunks
    std::string decoded;
    auto onChar = [&](uint8_t ch) {
        decoded.push_back(static_cast<char>(ch));
    };

    constexpr int kChunk = 512;
    for (size_t offset = 0; offset < audio.size(); offset += kChunk) {
        int n = static_cast<int>(std::min<size_t>(kChunk, audio.size() - offset));
        modem.feedAudio(audio.data() + offset, n, onChar);
    }

    // 3. Flush trailing buffered FEC blocks at end of transmission
    modem.flush(onChar);

    std::cout << "Decoded: " << decoded << "\n"
              << "Linear SNR: " << modem.snr() << "\n"
              << "Freq offset: " << modem.freqOffsetHz() << " Hz\n";

    // 4. Reset decoder state before the next independent transmission
    modem.reset();
    return 0;
}
```

Compile and run against the built library:

```bash
g++ -std=c++20 -O2 loopback.cpp -Iinclude build/liboliviamodem.a -lm -o loopback
./loopback
```

---

## Olivia Submodes & Performance

Olivia modes are written as **`Tones/Bandwidth`** (e.g., `8/500`). Bandwidth controls the spectral footprint and throughput, while tone count trades baud rate for deeper time/frequency interleaving and stronger weak-signal sensitivity.

All values below are computed directly from the DSP formulas in [`include/jalocha/pj_mfsk.h`](include/jalocha/pj_mfsk.h) at `SampleRate = 8000 Hz` and `SymbolsPerBlock = 64` (7-bit ASCII):

| Mode | Tones | Bits/Sym | Bandwidth | Tone Spacing / Baud | Block Period | Chars/sec | Speed (5-ch WPM) | Tune Margin (`syncMargin=8`) | Typical Use |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **4/125** | 4 | 2 | 125 Hz | 31.25 Hz / 31.25 bd | 2.05 s | 0.98 | 11.7 WPM | ±125.0 Hz | Ultra-narrow, crowded bands |
| **4/250** | 4 | 2 | 250 Hz | 62.50 Hz / 62.50 bd | 1.02 s | 1.95 | 23.4 WPM | ±250.0 Hz | Narrow, weak-signal HF |
| **8/250** | 8 | 3 | 250 Hz | 31.25 Hz / 31.25 bd | 2.05 s | 1.46 | 17.6 WPM | ±125.0 Hz | Narrow with deeper FEC |
| **8/500** | 8 | 3 | 500 Hz | 62.50 Hz / 62.50 bd | 1.02 s | 2.93 | 35.2 WPM | ±250.0 Hz | **Most common HF calling mode** |
| **16/500** | 16 | 4 | 500 Hz | 31.25 Hz / 31.25 bd | 2.05 s | 1.95 | 23.4 WPM | ±125.0 Hz | Strong weak-signal / NVIS QSB |
| **8/1000** | 8 | 3 | 1000 Hz | 125.00 Hz / 125.00 bd | 0.51 s | 5.86 | 70.3 WPM | ±500.0 Hz | Fast keyboard-to-keyboard |
| **16/1000** | 16 | 4 | 1000 Hz | 62.50 Hz / 62.50 bd | 1.02 s | 3.91 | 46.9 WPM | ±250.0 Hz | Balanced fast HF mode |
| **32/1000** | 32 | 5 | 1000 Hz | 31.25 Hz / 31.25 bd | 2.05 s | 2.44 | 29.3 WPM | ±125.0 Hz | High sensitivity / deep FEC |
| **16/2000** | 16 | 4 | 2000 Hz | 125.00 Hz / 125.00 bd | 0.51 s | 7.81 | 93.8 WPM | ±500.0 Hz | High-speed wide channel |
| **32/2000** | 32 | 5 | 2000 Hz | 62.50 Hz / 62.50 bd | 1.02 s | 4.88 | 58.6 WPM | ±250.0 Hz | Wide channel, robust FEC |
| **64/2000** | 64 | 6 | 2000 Hz | 31.25 Hz / 31.25 bd | 2.05 s | 2.93 | 35.2 WPM | ±125.0 Hz | Maximum standard FEC depth |

> **Note on WPM:** Words-per-minute above uses the standard 5-characters-per-word convention (`chars/sec × 12`). Using 6 characters per word (5 letters + space), multiply `chars/sec` by 10 (e.g., `8/500` ≈ 29.3 WPM, `32/1000` ≈ 24.4 WPM).

---

## Repository Layout

```text
olivia-modem-fork/
├── CMakeLists.txt                # CMake build and install rules (liboliviamodem.a)
├── README.md                     # Project overview and quick start
├── CHANGELOG.md                  # Fork changelog
├── AGENTS.md                     # AI coding agent workflow & guardrails
├── .markdownlint-cli2.jsonc      # Markdown lint configuration
├── include/
│   ├── oliviamodem.h             # Public C++20 API (OliviaConfig, OliviaLib)
│   └── jalocha/                  # Pawel Jalocha's header-only DSP templates
│       ├── pj_mfsk.h             # MFSK_Transmitter, MFSK_Receiver, FEC, demod, sync
│       ├── pj_fft.h              # Radix-2 FFT with dual-real packing (r2FFT)
│       ├── pj_fht.h              # Forward & inverse Fast Hadamard Transform
│       ├── pj_cmpx.h             # Complex number arithmetic (Cmpx<T>)
│       ├── pj_gray.h             # Binary <-> Gray code conversion
│       ├── pj_lowpass3.h         # 3rd-order IIR smoothing filter (LowPass3_Filter)
│       ├── pj_fifo.h             # Ring-buffer FIFO (FIFO<T>)
│       └── pj_struc.h            # Dynamic arrays, Seq<T>, SampleBuffer<T>
├── src/
│   └── oliviamodem.cpp           # OliviaLib PIMPL implementation & tone generator
└── docs/
    ├── api-reference.md          # Complete public API reference
    ├── dsp-architecture.md       # Protocol & DSP internals
    ├── integration-guide.md      # Build, audio pipeline, recipes & troubleshooting
    └── contributing.md           # Fork workflow, linting & testing
```

---

## Integration with `jf8call-fork`

`olivia-modem-fork` is linked as an optional static library by [`jf8call-fork`](../jf8call-fork) (our fork of [`jfrancis42/jf8call`](https://github.com/jfrancis42/jf8call)), which auto-discovers `../olivia-modem-fork` via `CMakeLists.txt` or accepts `-DOLIVIA_MODEM_DIR=<path>`. Inside `jf8call-fork`, `OliviaModem` implements the `IModem` interface alongside [`gfsk8-modem-clean-fork`](../gfsk8-modem-clean-fork) (JS8/GFSK8), Codec2, and [`libpsk-fork`](../libpsk-fork) (PSK31/63/125)—wrapping `OliviaLib` with a `std::mutex`, `int16_t` ↔ `double` conversion, stream sync/EOM framing, and 7 built-in submodes (`8/500`, `4/250`, `8/250`, `16/500`, `8/1000`, `16/1000`, `32/1000`).

---

## Credits & License

- **DSP Core:** Designed and implemented by **Pawel Jalocha (SP9VRC)** (2003–2004), originator of the Olivia digital mode.
- **Upstream Maintenance:** Integrated and maintained in **fldigi** by **Dave Freese (W1HKJ)** and the fldigi contributors.
- **Original Standalone Wrapper:** Created by **Jeff Francis (N0GQ)** in [`jfrancis42/olivia-modem`](https://github.com/jfrancis42/olivia-modem) to expose Pawel's DSP core as a dependency-free static library. This repository (`olivia-modem-fork`) is forked from that upstream project with repository cleanup, documentation, and CI/integration updates.

**License:** `GPL-3.0-or-later` — inherited from fldigi and Pawel Jalocha's original Olivia DSP templates. All public headers and source files carry `SPDX-License-Identifier: GPL-3.0-or-later`.
