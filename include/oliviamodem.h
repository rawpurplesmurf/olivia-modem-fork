#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// oliviamodem.h — Standalone Olivia MFSK modem library
//
// Wraps Pawel Jalocha's MFSK_Transmitter/MFSK_Receiver templates from fldigi
// with a clean C++ interface that has no FLTK, no fldigi globals, and no
// external audio library dependencies.
//
// Audio is always 8000 Hz, floating-point double, range [-1.0 .. +1.0].

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ── Configuration ─────────────────────────────────────────────────────────────

struct OliviaConfig {
    int    tones       = 8;     ///< Number of tones: 4, 8, 16, 32, or 64
    int    bandwidthHz = 500;   ///< Bandwidth: 125, 250, 500, 1000, or 2000 Hz
    int    syncMargin  = 8;     ///< Frequency search margin in tone spacings
    int    syncIntegLen= 4;     ///< Synchroniser integration period (FEC blocks)
    double squelch     = 0.0;   ///< SNR threshold for printing (0 = open squelch)
    bool   sendTones   = true;  ///< Prepend/append preamble/postamble tones
};

// ── OliviaLib ─────────────────────────────────────────────────────────────────

/// Standalone Olivia MFSK modem.
/// Not thread-safe — protect concurrent calls with an external mutex if needed.
class OliviaLib {
public:
    explicit OliviaLib(const OliviaConfig &cfg = OliviaConfig{});
    ~OliviaLib();

    // Non-copyable; moveable.
    OliviaLib(const OliviaLib &) = delete;
    OliviaLib &operator=(const OliviaLib &) = delete;
    OliviaLib(OliviaLib &&) noexcept;
    OliviaLib &operator=(OliviaLib &&) noexcept;

    // ── TX ────────────────────────────────────────────────────────────────────

    /// Encode @p text to double PCM at 8000 Hz, centred on @p carrierHz.
    /// Returns the complete audio waveform including optional preamble/postamble
    /// tones.  May return an empty vector on encoding failure.
    std::vector<double> modulate(const std::string &text, double carrierHz) const;

    // ── RX ────────────────────────────────────────────────────────────────────

    /// Feed @p count audio samples (8000 Hz, double) to the decoder.
    /// For each decoded character @p cb is called synchronously.
    void feedAudio(const double *samples, int count,
                   std::function<void(uint8_t)> cb);

    /// Flush any buffered partial FEC block, calling @p cb for each decoded char.
    void flush(std::function<void(uint8_t)> cb);

    /// Reset receiver state (clears FEC buffers and synchroniser).
    void reset();

    // ── Metrics ───────────────────────────────────────────────────────────────

    double snr()          const; ///< Current RX SNR (linear, not dB)
    double freqOffsetHz() const; ///< Estimated RX carrier offset in Hz

    // ── Info ──────────────────────────────────────────────────────────────────

    static constexpr int kSampleRate = 8000;

    int  tones()       const;
    int  bandwidthHz() const;
    OliviaConfig config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
