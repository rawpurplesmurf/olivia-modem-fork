# Contributing & Maintenance Guide

This guide documents upstream provenance, development rules, branch and Pull Request conventions, linting, and verification procedures for `olivia-modem-fork`.

---

## Repository & Fork Relationships

| Repository | Role |
| --- | --- |
| **`olivia-modem-fork`** (this repo) | Maintained fork with repository cleanup, technical documentation, CI linting, and static-library integration (`liboliviamodem.a`). |
| [`jfrancis42/olivia-modem`](https://github.com/jfrancis42/olivia-modem) | Original upstream standalone wrapper by Jeff Francis (N0GQ), extracted from [fldigi](https://sourceforge.net/projects/fldigi/) / Pawel Jalocha (SP9VRC). |
| [`jf8call-fork`](../../jf8call-fork) | Primary downstream consumer (`src/olivia_modem.h`, `src/olivia_modem.cpp`), forked from [`jfrancis42/jf8call`](https://github.com/jfrancis42/jf8call). |
| [`gfsk8-modem-clean-fork`](../../gfsk8-modem-clean-fork) | Sibling zero-dependency JS8/GFSK8 modem library fork used alongside `olivia-modem-fork` in `jf8call-fork`. |
| [`libpsk-fork`](../../libpsk-fork) | Sibling zero-dependency PSK31/63/125 modem library fork used alongside `olivia-modem-fork` in `jf8call-fork`. |

---

## Core Technical Guardrails

1. **Zero External Dependencies:**
   The library must remain strictly dependency-free beyond the **C++20 standard library** and **`libm`**. Do not add dependencies on FLTK, Qt, `libsamplerate`, `libsndfile`, FFTW, or Boost.
2. **Clean Public ABI (`include/oliviamodem.h`):**
   Keep Jalocha DSP headers (`include/jalocha/*.h`) isolated behind `OliviaLib::Impl` so consumers never inherit Jalocha's global-namespace macros or template instantiations.
3. **Wire Interoperability with fldigi:**
   Preserve Olivia's 7-bit Walsh–Hadamard FEC, `ScramblingCodeOlivia` (`0xE257E6D0291574EC`), cyclic offset `13`, diagonal interleaving, Gray coding, raised-cosine symbol shaping, and `FirstCarrierMultiplier` frequency alignment so signals remain 100% interoperable with fldigi.
4. **License Preservation (`GPL-3.0-or-later`):**
   Every new or modified source/header file must carry the `SPDX-License-Identifier: GPL-3.0-or-later` header and preserve upstream copyright notices in `include/jalocha/`.

---

## Branch & Pull Request Workflow

1. **Never commit or push directly to `main`.** Create an isolated branch named:
   - `feat/<short-description>` for new features or API additions
   - `fix/<short-description>` for bug fixes
   - `docs/<short-description>` for documentation updates
2. Update [`CHANGELOG.md`](../CHANGELOG.md) under `## [Unreleased]` for user-visible changes.
3. Open a Pull Request targeting `main`.
4. Wait for automated code review and CI checks to pass, then merge via `rebase-merge`.

---

## Verification Checklist

### 1. Build Verification

Confirm clean CMake compilation with `-Wall -Wextra`:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra"
cmake --build build
```

Verify that `build/liboliviamodem.a` is generated with no unexpected external symbol dependencies:

```bash
nm -u build/liboliviamodem.a | head -n 40
```

### 2. Loopback Sanity Test

Compile and run a standalone TX → RX loopback test (see [Integration Guide — Recipe 2](integration-guide.md#recipe-2-awgn-loopback-self-test)) across multiple modes (at minimum `8/500`, `4/250`, `16/500`, `32/1000`) to verify exact character round-tripping and sensible `snr()` / `freqOffsetHz()` readings.

### 3. Downstream Consumer Check (`jf8call-fork`)

If public signatures in `include/oliviamodem.h` or build outputs in `CMakeLists.txt` change, rebuild [`jf8call-fork`](../../jf8call-fork) pointing at the updated `olivia-modem-fork` checkout and run its unit tests:

```bash
cmake -B build -DOLIVIA_MODEM_DIR=/path/to/olivia-modem-fork
cmake --build build
ctest --test-dir build --output-on-failure
```

### 4. Markdown Linting

All `*.md` files are linted in CI using [`.markdownlint-cli2.jsonc`](../.markdownlint-cli2.jsonc). If `markdownlint-cli2` is installed locally, run:

```bash
markdownlint-cli2 "**/*.md"
```

When authoring Markdown:

- Use ATX headings (`#`, `##`, `###`) incrementing by one level at a time.
- Fence all code blocks with explicit language tags (`bash`, `cpp`, `cmake`, `text`, `mermaid`) and never prefix shell commands with `$ `.
- Avoid hard tabs and ensure every Markdown file ends with a single newline.
