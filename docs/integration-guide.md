# Integration & Audio Pipeline Guide

This guide covers building `olivia-modem`, integrating it into CMake or standalone C++20 projects, wiring it to real-world audio streams (48 kHz soundcards, SDR IQ pipelines, WAV files), tuning parameters for HF conditions, and troubleshooting common issues.

---

## Building from Source

### Standard Release Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/liboliviamodem.a`.

### Debug Build with Compiler Warnings

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"
cmake --build build-debug
```

### Installing Locally or to a Sysroot

```bash
cmake --install build --prefix /usr/local
```

This installs:

- `${CMAKE_INSTALL_LIBDIR}/liboliviamodem.a`
- `${CMAKE_INSTALL_INCLUDEDIR}/oliviamodem.h`
- `${CMAKE_INSTALL_INCLUDEDIR}/jalocha/*.h`

### Cross-Compiling for Raspberry Pi (`aarch64`)

Because `olivia-modem` has no external dependencies beyond the C++20 standard library and `libm`, cross-compiling only requires a C++20 cross-compiler:

```bash
cmake -B build-arm64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
cmake --build build-arm64
```

---

## Integrating with CMake

### Option 1: `add_subdirectory` (Recommended for Monorepos / Submodules)

```cmake
add_subdirectory(external/olivia-modem-fork)

add_executable(my_radio_app src/main.cpp)
target_link_libraries(my_radio_app PRIVATE oliviamodem)
```

`oliviamodem` automatically exports `include/` as a `PUBLIC` include directory and links `libm` (`m`) publicly.

### Option 2: Prebuilt `IMPORTED` Static Target (`jf8call-fork` Pattern)

This is the pattern used by [`jf8call-fork`](../../jf8call-fork) to auto-discover and link `olivia-modem-fork` as an optional sibling checkout. `jf8call-fork`'s `CMakeLists.txt` searches for `include/oliviamodem.h` in this order unless `-DOLIVIA_MODEM_DIR` is explicitly provided:

1. `-DOLIVIA_MODEM_DIR=<path>` (CMake cache variable)
2. `$ENV{OLIVIA_MODEM_DIR}` (environment variable)
3. `../olivia-modem`
4. `../olivia-modem-fork`
5. `$HOME/olivia-modem-fork`
6. `$HOME/olivia-modem`

It then locates the built static archive at `${OLIVIA_MODEM_BUILD_DIR}/liboliviamodem.a` (if set), `${OLIVIA_MODEM_DIR}/build/liboliviamodem.a`, or `${OLIVIA_MODEM_DIR}/liboliviamodem.a`:

```cmake
set(OLIVIA_MODEM_DIR "${CMAKE_SOURCE_DIR}/../olivia-modem-fork"
    CACHE PATH "Path to olivia-modem-fork")

if(EXISTS "${OLIVIA_MODEM_DIR}/include/oliviamodem.h" AND
   EXISTS "${OLIVIA_MODEM_DIR}/build/liboliviamodem.a")
    add_library(oliviamodem STATIC IMPORTED)
    set_target_properties(oliviamodem PROPERTIES
        IMPORTED_LOCATION "${OLIVIA_MODEM_DIR}/build/liboliviamodem.a")
    target_include_directories(oliviamodem INTERFACE
        "${OLIVIA_MODEM_DIR}/include")

    target_compile_definitions(my_radio_app PRIVATE HAVE_OLIVIA)
    target_link_libraries(my_radio_app PRIVATE oliviamodem m)
endif()
```

### Option 3: Direct Compiler Invocation (Without CMake)

```bash
g++ -std=c++20 -O2 main.cpp \
    -I/path/to/olivia-modem-fork/include \
    /path/to/olivia-modem-fork/build/liboliviamodem.a \
    -lm -o my_radio_app
```

---

## Audio Pipeline Integration

### Sample Format Contract

`OliviaLib` always operates at **`OliviaLib::kSampleRate` (8000 Hz)**, mono, floating-point `double` normalized to **`[-1.0, +1.0]`**.

#### Converting `int16_t` ↔ `double`

```cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// RX: int16_t PCM -> double [-1.0, +1.0]
std::vector<double> int16ToDouble(const int16_t *in, size_t n) {
    std::vector<double> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<double>(in[i]) / 32768.0;
    return out;
}

// TX: double [-1.0, +1.0] -> int16_t PCM with headroom (e.g., 0.85x to avoid ALC clipping)
std::vector<int16_t> doubleToInt16(const std::vector<double> &in, double gain = 0.85) {
    std::vector<int16_t> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        double v = std::clamp(in[i] * gain, -1.0, 1.0);
        out[i] = static_cast<int16_t>(std::lround(v * 32767.0));
    }
    return out;
}
```

### Interfacing with 48 kHz Soundcards (PipeWire / PulseAudio / ALSA)

Most Linux audio stacks capture and play back at **48000 Hz** (an exact 6:1 integer ratio to 8000 Hz):

- **RX (48 kHz → 8 kHz):** Always apply a low-pass anti-aliasing filter (cutoff ≤ 3400 Hz, stopband ≥ 4000 Hz) **before** decimating by 6. Naive 1-in-6 subsampling without low-pass filtering will alias energy above 4000 Hz into the Olivia passband and degrade weak-signal decoding.
- **TX (8 kHz → 48 kHz):** Zero-stuff by 6 and apply an interpolation low-pass filter (cutoff ~3400 Hz, gain 6×) or use a polyphase resampler (`libsamplerate`, `soxr`, or a windowed-sinc FIR filter) so harmonic images above 4 kHz are suppressed before reaching the transceiver mic/line input.

### RX Carrier Frequency and Tuning

`OliviaLib` initializes its internal receiver centered at **`1500.0 Hz`**:

- For standard SSB audio interfaces (2.4–3.0 kHz passband), centering the Olivia signal at **1500 Hz** keeps even the widest `64/2000` mode (`515.6–2484.4 Hz`) comfortably inside the SSB filter passband.
- Small tuning mismatches up to **`±syncMargin * (8000 / SymbolLen)` Hz** are acquired automatically by the receiver (`±250 Hz` for `8/500`, `±125 Hz` for `16/500` and `32/1000` at default `syncMargin = 8`).
- If your application needs to decode an Olivia signal centered at an arbitrary audio frequency $f_{\text{rx}} \ne 1500\text{ Hz}$ (for example, clicking a waterfall signal at `900 Hz` or `2100 Hz`), frequency-shift the 8 kHz audio by $\Delta f = 1500 - f_{\text{rx}}$ (using an analytic Hilbert/complex mixer or SSB weaver shifter) before passing it to `feedAudio()`, or retune the rig VFO by $-\Delta f$.

### Chunk Sizes and Decode Latency

- **Recommended `feedAudio()` chunk size:** `512` or `1024` samples (`64 ms` or `128 ms` at 8000 Hz). Any chunk size is accepted; the receiver buffers internally.
- **Expected latency:**
  1. **STFT pre-processor buffering:** `WindowLen = 16 * SymbolLen` samples (`0.512 s` for `8/500`; `1.024 s` for `16/500` and `32/1000`).
  2. **FEC block + synchronizer pipeline:** Approximately `(syncIntegLen + 0.5) * BlockPeriod` seconds before the first characters emerge from `DecodePipe` (for `8/500` with `syncIntegLen = 4`, expect ~5 seconds from start of data to first decoded characters; for `32/1000`, ~10 seconds).
  3. **End-of-stream flushing:** Always call `flush(cb)` when decoding finite recordings or when a known transmission ends so the final buffered FEC blocks are emitted.

---

## Code Recipes

### Recipe 1: Thread-Safe Streaming Receiver Wrapper

Because `OliviaLib` is not thread-safe, real-time audio callbacks (which typically run on an audio worker thread) should guard receiver calls with a `std::mutex`:

```cpp
#include "oliviamodem.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

class SafeOliviaReceiver {
public:
    explicit SafeOliviaReceiver(const OliviaConfig &cfg)
        : m_modem(cfg) {}

    void onAudio(const double *samples, int count,
                 const std::function<void(const std::string &, double, double)> &onLine)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_modem.feedAudio(samples, count, [&](uint8_t ch) {
            if (ch == '\r') return;
            if (ch == '\n') {
                if (!m_lineBuf.empty()) {
                    onLine(m_lineBuf, m_modem.snr(), 1500.0 + m_modem.freqOffsetHz());
                    m_lineBuf.clear();
                }
            } else {
                m_lineBuf.push_back(static_cast<char>(ch));
            }
        });
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_modem.reset();
        m_lineBuf.clear();
    }

private:
    std::mutex m_mutex;
    OliviaLib  m_modem;
    std::string m_lineBuf;
};
```

### Recipe 2: AWGN Loopback Self-Test

Use this pattern to verify a build or measure decoding behavior under additive white Gaussian noise:

```cpp
#include "oliviamodem.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int main() {
    OliviaConfig cfg;
    cfg.tones        = 8;
    cfg.bandwidthHz  = 500;
    cfg.syncMargin   = 8;
    cfg.syncIntegLen = 4;
    cfg.squelch      = 0.0;
    cfg.sendTones    = true;

    OliviaLib modem(cfg);

    const std::string txText = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890\n";
    std::vector<double> pcm = modem.modulate(txText, 1500.0);

    // Optional: add AWGN (sigma = 0.25) and leading/trailing silence
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.25);

    std::vector<double> rxStream(4000, 0.0); // 0.5 s leading silence
    for (double s : pcm)
        rxStream.push_back(s + noise(rng));
    rxStream.insert(rxStream.end(), 4000, 0.0); // 0.5 s trailing silence

    std::string rxText;
    auto cb = [&](uint8_t ch) { rxText.push_back(static_cast<char>(ch)); };

    constexpr int kBlock = 512;
    for (size_t i = 0; i < rxStream.size(); i += kBlock) {
        int n = static_cast<int>(std::min<size_t>(kBlock, rxStream.size() - i));
        modem.feedAudio(rxStream.data() + i, n, cb);
    }
    modem.flush(cb);

    std::cout << "TX: " << txText
              << "RX: " << rxText << "\n"
              << "SNR (linear): " << modem.snr()
              << ", Offset: " << modem.freqOffsetHz() << " Hz\n";
    return 0;
}
```

---

## Tuning `OliviaConfig` for HF Channel Conditions

| Scenario | Recommended Mode | `syncMargin` | `syncIntegLen` | `squelch` | Rationale |
| --- | --- | --- | --- | --- | --- |
| **General HF QSO (default)** | `8/500` | `8` (±250 Hz) | `4` | `0.0` (floor 3.0) | Standard calling mode; good balance of speed, latency, and sensitivity. |
| **Weak-signal DX / below noise floor** | `16/500` or `32/1000` | `8` (±125 Hz) | `4` – `8` | `0.0` | Deeper Walsh–Hadamard + interleaving gain; longer `syncIntegLen` improves lock at very low SNR. |
| **Narrow / crowded band** | `4/250` or `8/250` | `4` – `8` | `4` | `0.0` | Narrow 250 Hz footprint fits between adjacent QRM signals. |
| **Fast keyboard chat (strong path)** | `16/1000` or `8/1000` | `8` | `2` – `4` | `3.5` – `5.0` | Shorter `syncIntegLen` cuts turn-around latency; higher squelch reduces noise gibberish between overs. |
| **Tight CPU / embedded (Raspberry Pi Zero)** | `8/500` | `4` | `4` | `0.0` | Halving `syncMargin` nearly halves parallel `MFSK_SoftDecoder` CPU work (`2*(2*4+1) = 18` vs `34` decoders). |

---

## Troubleshooting & FAQ

### No characters are decoded during loopback testing

Check these four common causes:

1. **Missing `flush()` at end of buffer:** Olivia's synchronizer buffers `syncIntegLen` FEC blocks plus `WindowLen` samples. Short test messages will remain inside `DecodePipe` unless you either feed trailing silence or call `modem.flush(cb)`.
2. **TX carrier far from `1500.0 Hz`:** `OliviaLib` always listens around `1500.0 Hz`. If you called `modulate(text, 1000.0)` with `8/500` and `syncMargin = 8` (±250 Hz), the signal lies outside the `1250–1750 Hz` search window. Transmit at `1500.0 Hz` or shift the RX audio to `1500.0 Hz`.
3. **Sample rate mismatch:** Verify input samples are genuinely `8000 Hz`. Feeding `48000 Hz` or `44100 Hz` audio directly compresses/stretches all tone spacings by 6× or 5.5× and will never decode.
4. **Mismatched mode parameters:** Both ends must agree on `tones` and `bandwidthHz`.

### Why does `squelch = 0.0` still suppress very weak/noise-only audio?

`MFSK_Receiver::Preset()` enforces a hard lower bound of `SyncThreshold >= 3.0`. Setting `cfg.squelch = 0.0` selects the most permissive threshold allowed by the Jalocha synchronizer (`3.0`), preventing continuous random noise output when no Olivia signal is present.

### Why are non-ASCII characters or low control bytes missing?

- Olivia's standard FEC alphabet is **7-bit ASCII** (`0x00`–`0x7F`); `MFSK_Encoder` masks every byte with `0x7F`.
- `OliviaLib::feedAudio()` and `OliviaLib::flush()` filter out bytes `<= 7` (`0x00`–`0x07`) so idle NUL padding bytes (`0x00`) do not pollute application text streams.

### High CPU usage with large `syncMargin`

Each increment of `syncMargin` adds `4` parallel `MFSK_SoftDecoder` instances (`2` half-symbol slices × `2` additional frequency bins, plus `128 × 2` extra `LowPass3_Filter` states and `DecodePipe` slots). Keep `syncMargin` in the `4–12` range unless wide blind capture is strictly needed.

### Decoded text has trailing spaces or stray characters at the end of an over

When a transmitter stops mid-block, `MFSK_Transmitter` pads remaining character slots in the final FEC block with `0x00` (which `OliviaLib` filters out). On noisy channels, the final decaying symbol during `flush()` can occasionally decode as a stray character right before `SyncSNR` drops below `SyncThreshold`. Framing messages with an explicit end-of-message delimiter (such as `\n` or an ASCII control byte `>= 0x08`, as done in [`jf8call-fork`](../../jf8call-fork)'s `OliviaModem`) cleanly separates valid payloads from trailing noise.
