# API Reference

This document specifies the complete public C++20 API defined in [`include/oliviamodem.h`](../include/oliviamodem.h) and implemented in [`src/oliviamodem.cpp`](../src/oliviamodem.cpp).

```cpp
#include "oliviamodem.h"
```

All public symbols reside in the global namespace. Internal Jalocha DSP templates (`include/jalocha/*.h`) are not included by `oliviamodem.h`; they are hidden behind the `OliviaLib::Impl` PIMPL boundary.

---

## Constants

| Symbol | Type | Value | Description |
| --- | --- | --- | --- |
| `OliviaLib::kSampleRate` | `static constexpr int` | `8000` | Fixed audio sample rate in Hz for both `modulate()` output and `feedAudio()` input. |

---

## `struct OliviaConfig`

Configuration structure passed to the `OliviaLib` constructor. Every member has a default value matching standard Olivia `8/500` operation.

```cpp
struct OliviaConfig {
    int    tones        = 8;     // Number of tones: 4, 8, 16, 32, or 64
    int    bandwidthHz  = 500;   // Bandwidth: 125, 250, 500, 1000, or 2000 Hz
    int    syncMargin   = 8;     // Frequency search half-width (in FFT bins)
    int    syncIntegLen = 4;     // Synchronizer integration length (FEC blocks)
    double squelch      = 0.0;   // SNR threshold for decoded output
    bool   sendTones    = true;  // Prepend synchronization preamble tones on TX
};
```

### Field Specifications & Internal Clamping

When `OliviaLib` initializes its receiver (and on every `modulate()` call for the transmitter), `MFSK_Receiver::Preset()` and `MFSK_Transmitter::Preset()` sanitize parameters to valid powers of two and safe lower/upper bounds:

| Field | Default | Standard Values | Internal Enforcement in `Preset()` | Notes |
| --- | --- | --- | --- | --- |
| `tones` | `8` | `4`, `8`, `16`, `32`, `64` | Clamped to `[2, 256]`, then rounded down to a power of two (`1 << floor(log2(tones))`). | Determines `BitsPerSymbol = log2(tones)` (2–6 for standard modes). |
| `bandwidthHz` | `500` | `125`, `250`, `500`, `1000`, `2000` | Clamped to `[125, 2000]`, then rounded down to `125 * 2^floor(log2(bw/125))`. | Controls tone spacing `bandwidthHz / tones` and symbol rate. |
| `syncMargin` | `8` | `2` – `32` | Minimum `2`; clamped upper bound `<= Demodulator.FirstCarrier`. | **Measured in FFT bins**, not tone spacings (`CarrierSepar = 2` bins per tone). Search range is `±syncMargin * (8000 / SymbolLen)` Hz = `±(syncMargin / 2)` tone spacings. Larger values linearly increase CPU and RAM usage. |
| `syncIntegLen` | `4` | `2` – `16` | Minimum `2`. | Sets the 3rd-order IIR filter weight `1.0 / syncIntegLen` and `DecodePipe` depth. Higher values improve weak-signal lock at the expense of longer acquisition latency. |
| `squelch` | `0.0` | `0.0` – `90.0` | First clamped to `[0.0, 90.0]` in `oliviamodem.cpp`, then **floored to `>= 3.0`** inside `MFSK_Receiver::Preset()`. | Linear SNR threshold (`SyncSNR >= SyncThreshold`). Setting `0.0` gives the lowest possible threshold (`3.0`), not an unrestricted zero-threshold gate. |
| `sendTones` | `true` | `true`, `false` | Passed directly to `modulate()`. | When `true`, prepends 8192 samples (1.024 s) of dual alternating synchronization tones at `carrierHz ± bandwidthHz / 2`. See [Known Behaviors & Caveats](#known-behaviors--caveats) regarding postamble tones. |

> **Important:** `OliviaLib::tones()`, `OliviaLib::bandwidthHz()`, and `OliviaLib::config()` return the original `OliviaConfig` values passed to the constructor, not any clamped values adjusted internally by `Preset()`. Always pass valid power-of-two `tones` and standard `bandwidthHz` values so the reported config matches the DSP engine.

---

## `class OliviaLib`

Standalone Olivia MFSK transmitter and stateful streaming receiver.

### Construction, Destruction & Ownership

```cpp
explicit OliviaLib(const OliviaConfig &cfg = OliviaConfig{});
~OliviaLib();

OliviaLib(const OliviaLib &) = delete;
OliviaLib &operator=(const OliviaLib &) = delete;
OliviaLib(OliviaLib &&) noexcept;
OliviaLib &operator=(OliviaLib &&) noexcept;
```

- **`OliviaLib(const OliviaConfig &cfg)`**
  - Allocates the internal `Impl` containing a stateful `MFSK_Receiver<double>` configured from `cfg`.
  - Centers the receiver passband at **`1500.0 Hz`** (`presetRx(1500.0)`).
  - **Exceptions:** Throws `std::runtime_error("OliviaLib: receiver Preset() failed")` if `MFSK_Receiver::Preset()` returns `< 0` (e.g., invalid FFT size or allocation failure).
- **Copy/Move semantics:** Non-copyable (owns heap-allocated DSP buffers); move-constructible and move-assignable (`noexcept`). Do not call methods on a moved-from `OliviaLib` instance other than destruction or move-assignment.

---

### Transmit (TX)

#### `modulate`

```cpp
std::vector<double> modulate(const std::string &text, double carrierHz) const;
```

Encodes `text` into a complete, self-contained mono PCM waveform at `8000 Hz` (`double`, nominally in `[-1.0, +1.0]`), centered on `carrierHz`.

- **Parameters:**
  - `text` — Payload bytes to transmit. Olivia uses 7-bit ASCII (`Char & 0x7F`); bytes with bit 7 set are masked to 7 bits by `MFSK_Encoder::EncodeCharacter()`.
  - `carrierHz` — Center frequency of the Olivia passband in Hz (typically `1500.0`). Ensure `carrierHz - bandwidthHz / 2 > 0` and `carrierHz + bandwidthHz / 2 < 4000.0` (Nyquist at 8000 Hz).
- **Return value:**
  - A `std::vector<double>` containing the complete audio waveform at 8000 Hz, or an **empty vector** if internal `MFSK_Transmitter::Preset()` fails.
- **Behavior details:**
  - **Stateless:** Constructs a fresh local `MFSK_Transmitter<double>` per call. Does not modify the receiver state inside `m_impl->rx`.
  - **Preamble:** If `cfg.sendTones` is `true`, prepends `8192` samples (`1.024 s`) of alternating tones at `carrierHz - bandwidthHz / 2` and `carrierHz + bandwidthHz / 2` (four 2048-sample bursts `A, B, A, B` with 256-sample raised-cosine edge ramps).
  - **Leading idle byte & block padding:** Queues a leading NUL (`0x00`) byte before `text` (matching fldigi behavior so the encoder begins producing immediately), then calls `Stop()`. Any trailing partial FEC block (fewer than `BitsPerSymbol` characters) is zero-padded with `0x00` bytes.
  - **Amplitude scaling:** Each transmitted symbol batch (`SymbolSepar` samples) is peak-normalized inside `MFSK_Transmitter::Output()` so peak amplitude reaches `1.0`.

---

### Receive (RX)

The receiver is **stateful** across calls, maintaining an internal sample buffer, an STFT spectral pre-processor (`MFSK_InputProcessor`), an FFT demodulator (`MFSK_Demodulator`), a bank of parallel soft-decision Walsh–Hadamard decoders (`MFSK_SoftDecoder`), and a multi-block synchronization pipeline (`DecodePipe`).

#### `feedAudio`

```cpp
void feedAudio(const double *samples, int count,
               std::function<void(uint8_t)> cb);
```

Streams `count` mono 8000 Hz `double` samples (range `[-1.0, +1.0]`) into the decoder.

- **Parameters:**
  - `samples` — Pointer to input PCM samples. If `nullptr` or `count <= 0`, returns immediately without side effects.
  - `count` — Number of samples. Any positive batch size works; `512` or `1024` samples per call is recommended for responsive streaming.
  - `cb` — Synchronous callback invoked once for each decoded byte that passes the control-character filter.
- **Character filtering:**
  - Only bytes with value **`ch > 7`** are forwarded to `cb`.
  - Control bytes `0x00` (NUL, Olivia's idle/padding character) through `0x07` (BEL) are silently dropped.
  - Backspace (`0x08`), Tab (`0x09`), Line Feed (`0x0A`), Carriage Return (`0x0D`), printable 7-bit ASCII (`0x20`–`0x7E`), and other values `0x08`–`0x7F` are passed to `cb`.
- **Buffering & latency:**
  - Samples accumulate until at least `WindowLen = 16 * SymbolLen` samples are available (e.g., `4096` samples = `0.512 s` for `8/500`, `8192` samples = `1.024 s` for `16/500` and `32/1000`).
  - Characters are emitted in bursts of `BitsPerSymbol` bytes once synchronization exceeds `SyncThreshold` and the `DecodePipe` delay (`syncIntegLen` FEC blocks) has elapsed.

#### `flush`

```cpp
void flush(std::function<void(uint8_t)> cb);
```

Drains buffered samples and partially completed FEC blocks at the end of an audio stream or file.

- **Parameters:**
  - `cb` — Synchronous callback invoked for each newly decoded byte with `ch > 7`.
- **Behavior:**
  - Calls `MFSK_Receiver::Flush()`, which zero-pads the remaining `InputBuffer` to `WindowLen` and feeds `SymbolSepar * SymbolsPerBlock * SyncIntegLen * 2` zero samples through the receiver pipeline so all queued blocks emerge from `DecodePipe`.
  - Because `flush()` injects silence into the receiver state, call `reset()` afterward if you intend to reuse the same `OliviaLib` instance for a fresh unrelated stream.

#### `reset`

```cpp
void reset();
```

Clears all receiver state without reallocating memory: resets the rate converter, input sample buffer, `MFSK_InputProcessor`, all `MFSK_SoftDecoder` instances, `DecodePipe` histories, `SyncSignal` / `SyncNoiseEnergy` integrators, best-sync indices, `SyncSNR`, and the output character FIFO.

---

### Receiver Metrics

#### `snr`

```cpp
double snr() const;
```

Returns the receiver's current **linear** signal-to-noise ratio (`SyncSNR`), **not** in decibels.

- **Definition in DSP core:**
  - `SyncBestSignal` is the 3rd-order IIR-smoothed average Walsh–Hadamard correlation peak magnitude (`|Peak|`).
  - `BestNoise` is `sqrt(NoiseEnergy)`, where `NoiseEnergy` is the smoothed mean-square energy of the 63 non-peak Hadamard bins (`SqrSum / (SymbolsPerBlock - 1)`).
  - `snr()` returns `SyncBestSignal / BestNoise` (an amplitude-to-RMS ratio).
  - Returns `0.0` before sync is established, if `BestNoise == 0`, or if the internal sanity clamp `SyncSNR > 100` resets it to `0.0`.
- **Converting to dB:**
  - Because `SyncSNR` is an amplitude-to-RMS ratio, electrical power SNR in dB is $20 \log_{10}(\text{snr})$ (though some applications such as [`jf8call-fork`](../../jf8call-fork) display $10 \log_{10}(\max(\text{snr}, 10^{-6}))$ as a compact relative indicator). Note that this metric reflects post-FEC Hadamard correlation SNR, not raw 2500 Hz SSB channel SNR.

#### `freqOffsetHz`

```cpp
double freqOffsetHz() const;
```

Returns the estimated carrier frequency offset in **Hz** relative to the receiver's fixed nominal center frequency (`1500.0 Hz`):

$$\text{freqOffsetHz} = \left(\text{SyncBestFreqOffset} - \left\lfloor\frac{\text{FreqOffsets}}{2}\right\rfloor\right) \times \frac{8000}{\text{SymbolLen}}$$

- Positive values indicate the received signal is higher in frequency than `1500.0 Hz`; negative values indicate it is lower.
- Quantized to steps of one FFT bin (`8000 / SymbolLen` Hz, i.e., half a tone spacing: `31.25 Hz` for `8/500`, `15.625 Hz` for `16/500` and `32/1000`).
- Estimated audio center frequency is `1500.0 + modem.freqOffsetHz()`.

---

### Configuration Accessors

```cpp
int          tones()       const;
int          bandwidthHz() const;
OliviaConfig config()      const;
```

Return the `tones`, `bandwidthHz`, or full `OliviaConfig` copy that was supplied to the `OliviaLib` constructor.

---

## Thread Safety

`OliviaLib` is **not thread-safe**:

- Protect concurrent calls to `feedAudio()`, `flush()`, `reset()`, `snr()`, and `freqOffsetHz()` on the same instance with an external `std::mutex`.
- Although `modulate()` is `const` and instantiates a local `MFSK_Transmitter<double>` without mutating `m_impl->rx`, note two subtleties:
  1. `MFSK_Modulator::Send()` calls standard C `rand()` for inter-symbol phase randomization, which may not be thread-safe on all C runtimes.
  2. Do not call `modulate()` concurrently with move-assignment or destruction of the same `OliviaLib` instance.
- Separate `OliviaLib` instances have no shared library-level global state (aside from libc `rand()`).

---

## Error Handling Summary

| Operation | Failure Mode | Result |
| --- | --- | --- |
| `OliviaLib(cfg)` | `MFSK_Receiver::Preset() < 0` | Throws `std::runtime_error` |
| `modulate(text, carrierHz)` | `MFSK_Transmitter::Preset() < 0` | Returns empty `std::vector<double>` |
| `feedAudio(nullptr, ...)` or `count <= 0` | Invalid/empty input | No-op (returns immediately) |

---

## Known Behaviors & Caveats

1. **Fixed 1500 Hz RX Center Frequency:**
   `OliviaLib::Impl` calls `presetRx(1500.0)` at construction and does not currently expose a public `setRxCarrierHz()` method. Incoming audio must be centered near `1500.0 Hz` (within `±TuneMargin = ±syncMargin * (8000 / SymbolLen)` Hz). See the [Integration Guide](integration-guide.md#rx-carrier-frequency-and-tuning) for shifting off-center signals.

2. **Minimum Squelch Floor (`3.0`):**
   Even when `OliviaConfig::squelch = 0.0`, `MFSK_Receiver::Preset()` enforces `if (SyncThreshold < 3.0) SyncThreshold = 3.0;`. Signals with post-FHT linear `SyncSNR < 3.0` will not emit characters.

3. **Postamble Tones in `modulate()`:**
   `OliviaConfig::sendTones = true` always prepends the 8192-sample preamble tone sequence before the MFSK data. In the current `src/oliviamodem.cpp` implementation, the postamble check (`tx.DoPostambleYet() == 1`) sits inside `while (tx.Running())`; because `DoPostambleYet()` only returns `1` once `State == 0` (when `Running()` is already `0`), the postamble branch is not reached during the drain loop. Receivers synchronize cleanly on the preamble and Walsh–Hadamard FEC blocks without requiring postamble tones.

4. **7-Bit ASCII & Control-Character Filtering:**
   - Standard Olivia encodes 7-bit characters (`0x00`–`0x7F`). Non-ASCII UTF-8 bytes (`>= 0x80`) have their high bit masked off (`Char & 0x7F`).
   - `feedAudio()` and `flush()` suppress decoded bytes `0x00`–`0x07` (`if (ch > 7) cb(ch)`). If your application uses in-band framing delimiters (such as [`jf8call-fork`](../../jf8call-fork)'s sync/EOM markers), choose control codes `>= 0x08` or ASCII escape sequences.

5. **TX Sample Range & Per-Batch Normalization:**
   `MFSK_Transmitter::Output()` normalizes each symbol batch by its local maximum magnitude, producing peak values of `±1.0`. Apply modest output headroom (e.g., scale by `0.8`–`0.9` or clamp before `int16_t` conversion) before feeding a radio audio interface to avoid ALC clipping.

6. **Synchronizer Noise Energy Pointer Offset (Inherited from fldigi):**
   In `MFSK_Receiver::ProcessSymbol()` (`pj_mfsk.h:2337`), `BestNoise` reads `NoiseEnergyPtr->Output` after the frequency-offset loop has advanced `NoiseEnergyPtr`. This behavior is identical to upstream `fldigi`'s `pj_mfsk.h` and is preserved verbatim for wire/behavioral parity.
