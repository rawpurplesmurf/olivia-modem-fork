// SPDX-License-Identifier: GPL-3.0-or-later
// oliviamodem.cpp — Standalone Olivia MFSK modem implementation
//
// Uses Pawel Jalocha's MFSK_Transmitter / MFSK_Receiver templates directly,
// with no dependency on fldigi globals, FLTK, libsndfile, or libsamplerate.

#include "oliviamodem.h"

// Jalocha DSP templates (header-only, no external libs required)
#include "jalocha/pj_mfsk.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr int    k_scBlockSize    = 512;
static constexpr int    k_toneDuration   = k_scBlockSize * 16;  // 8192 samples
static constexpr int    k_sr4            = k_toneDuration / 4;  // 2048 samples
static constexpr int    k_sr4Ramp        = k_sr4 / 8;          // 256-sample edge ramp
static constexpr double k_pi             = M_PI;

// ── Clamp helper (avoids pulling in fldigi's misc.h) ─────────────────────────

template<class T>
static inline T olivia_clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ── Preamble tone generation ──────────────────────────────────────────────────
// Generates TONE_DURATION samples of two interleaved tones at freqA and freqB,
// with cosine amplitude shaping at the edges.  Matches fldigi olivia::send_tones().

static void generateTones(double freqA, double freqB, int sampleRate,
                           std::vector<double> &out)
{
    // Amplitude shape: cosine ramp at each end, flat in the middle
    double ampshape[k_sr4];
    for (int i = 0; i < k_sr4; ++i)
        ampshape[i] = 1.0;
    for (int i = 0; i < k_sr4Ramp; ++i) {
        double w = 0.5 * (1.0 - std::cos(k_pi * i / k_sr4Ramp));
        ampshape[i]             = w;
        ampshape[k_sr4 - 1 - i] = w;
    }

    double tonebuff[k_toneDuration] = {};

    // Tone A phase accumulator
    double phA = 0.0;
    double dpA = 2.0 * k_pi * freqA / sampleRate;

    // Tone B phase accumulator
    double phB = 0.0;
    double dpB = 2.0 * k_pi * freqB / sampleRate;

    for (int i = 0; i < k_sr4; ++i) {
        double sa = std::cos(phA) * ampshape[i];
        double sb = std::cos(phB) * ampshape[i];
        phA += dpA; if (phA > k_pi) phA -= 2.0 * k_pi;
        phB += dpB; if (phB > k_pi) phB -= 2.0 * k_pi;

        tonebuff[i]           = sa;   // A, ramp 0
        tonebuff[k_sr4   + i] = sb;   // B, ramp 0
        tonebuff[2*k_sr4 + i] = sa;   // A, ramp 1 (same shape)
        tonebuff[3*k_sr4 + i] = sb;   // B, ramp 1
    }

    out.insert(out.end(), tonebuff, tonebuff + k_toneDuration);
}

// ── Transmitter setup helper ──────────────────────────────────────────────────

static void setupTx(MFSK_Transmitter<double> &tx,
                    const OliviaConfig &cfg, double carrierHz)
{
    tx.Tones              = static_cast<size_t>(cfg.tones);
    tx.Bandwidth          = static_cast<size_t>(cfg.bandwidthHz);
    tx.SampleRate         = OliviaLib::kSampleRate;
    tx.OutputSampleRate   = OliviaLib::kSampleRate;
    tx.bContestia         = false;
    tx.Reverse            = 0;

    // fc_offset places the first carrier below centre so tones span ±bw/2
    double fc_offset = cfg.bandwidthHz * (1.0 - 0.5 / cfg.tones) / 2.0;
    tx.FirstCarrierMultiplier = static_cast<float>((carrierHz - fc_offset) / 500.0);
}

// ── Receiver setup helper ─────────────────────────────────────────────────────

static void setupRx(MFSK_Receiver<double> &rx,
                    const OliviaConfig &cfg, double carrierHz)
{
    rx.Tones             = static_cast<size_t>(cfg.tones);
    rx.Bandwidth         = static_cast<size_t>(cfg.bandwidthHz);
    rx.SyncMargin        = static_cast<size_t>(cfg.syncMargin);
    rx.SyncIntegLen      = static_cast<size_t>(cfg.syncIntegLen);
    rx.SyncThreshold     = static_cast<double>(olivia_clamp(cfg.squelch, 0.0, 90.0));
    rx.SampleRate        = OliviaLib::kSampleRate;
    rx.InputSampleRate   = OliviaLib::kSampleRate;
    rx.bContestia        = false;
    rx.Reverse           = 0;

    double fc_offset = cfg.bandwidthHz * (1.0 - 0.5 / cfg.tones) / 2.0;
    rx.FirstCarrierMultiplier = static_cast<float>((carrierHz - fc_offset) / 500.0);
}

// ── OliviaLib::Impl ───────────────────────────────────────────────────────────

struct OliviaLib::Impl {
    OliviaConfig cfg;

    // Receiver is stateful (tracks sync, accumulates FEC), kept alive across calls
    MFSK_Receiver<double> rx;

    // Last carrier used for Rx so we can detect changes
    double rxCarrier = 1500.0;
    bool   rxPreset  = false;

    void presetRx(double carrierHz) {
        setupRx(rx, cfg, carrierHz);
        if (rx.Preset() < 0)
            throw std::runtime_error("OliviaLib: receiver Preset() failed");
        rxCarrier = carrierHz;
        rxPreset  = true;
    }

    explicit Impl(const OliviaConfig &c) : cfg(c) {
        presetRx(1500.0);
    }
};

// ── OliviaLib public implementation ──────────────────────────────────────────

OliviaLib::OliviaLib(const OliviaConfig &cfg)
    : m_impl(std::make_unique<Impl>(cfg))
{}

OliviaLib::~OliviaLib() = default;

OliviaLib::OliviaLib(OliviaLib &&) noexcept = default;
OliviaLib &OliviaLib::operator=(OliviaLib &&) noexcept = default;

// ── TX ────────────────────────────────────────────────────────────────────────

std::vector<double> OliviaLib::modulate(const std::string &text,
                                        double carrierHz) const
{
    // Build a fresh transmitter per call — TX is stateless from the caller's
    // perspective (each modulate() produces a complete self-contained waveform).
    MFSK_Transmitter<double> tx;
    setupTx(tx, m_impl->cfg, carrierHz);
    if (tx.Preset() < 0)
        return {};

    std::vector<double> out;

    // Preamble tones
    if (m_impl->cfg.sendTones) {
        const int &bw = m_impl->cfg.bandwidthHz;
        double freqA = carrierHz - bw / 2.0;
        double freqB = carrierHz + bw / 2.0;
        generateTones(freqA, freqB, kSampleRate, out);
    }

    // The encoder needs at least one character before it will produce output.
    // Queue a null byte first (matches fldigi behaviour), then the payload,
    // then stop.
    tx.Start();
    tx.PutChar(0);
    for (unsigned char c : text)
        tx.PutChar(c);
    tx.Stop();

    // Drain audio until the transmitter finishes
    const size_t maxBatch = tx.MaxOutputLen;
    std::vector<double> batch(maxBatch);

    while (tx.Running()) {
        // Check if it's time to append the postamble
        if (tx.DoPostambleYet() == 1 && m_impl->cfg.sendTones) {
            const int &bw = m_impl->cfg.bandwidthHz;
            double freqA = carrierHz - bw / 2.0;
            double freqB = carrierHz + bw / 2.0;
            generateTones(freqA, freqB, kSampleRate, out);
            // Append a block of silence so the remote side has time to decode
            out.insert(out.end(), k_scBlockSize, 0.0);
        }
        int n = tx.Output(batch.data());
        if (n > 0)
            out.insert(out.end(), batch.data(), batch.data() + n);
        else if (n == 0)
            break;  // shouldn't happen, but guard against infinite loop
    }

    // Final drain pass after Running() drops
    int n = tx.Output(batch.data());
    if (n > 0)
        out.insert(out.end(), batch.data(), batch.data() + n);

    return out;
}

// ── RX ────────────────────────────────────────────────────────────────────────

void OliviaLib::feedAudio(const double *samples, int count,
                          std::function<void(uint8_t)> cb)
{
    if (!samples || count <= 0) return;

    m_impl->rx.Process(samples, static_cast<size_t>(count));

    uint8_t ch;
    while (m_impl->rx.GetChar(ch) > 0)
        if (ch > 7)  // skip control chars except printable content
            cb(ch);
}

void OliviaLib::flush(std::function<void(uint8_t)> cb)
{
    m_impl->rx.Flush();
    uint8_t ch;
    while (m_impl->rx.GetChar(ch) > 0)
        if (ch > 7)
            cb(ch);
}

void OliviaLib::reset()
{
    m_impl->rx.Reset();
}

// ── Metrics ───────────────────────────────────────────────────────────────────

double OliviaLib::snr() const {
    return m_impl->rx.SignalToNoiseRatio();
}

double OliviaLib::freqOffsetHz() const {
    return m_impl->rx.FrequencyOffset();
}

// ── Info ─────────────────────────────────────────────────────────────────────

int OliviaLib::tones()       const { return m_impl->cfg.tones; }
int OliviaLib::bandwidthHz() const { return m_impl->cfg.bandwidthHz; }
OliviaConfig OliviaLib::config() const { return m_impl->cfg; }
