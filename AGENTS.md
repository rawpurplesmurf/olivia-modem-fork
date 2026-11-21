# olivia-modem-fork — AI Coding Agent Instructions

Standalone C++20 Olivia MFSK static modem library (`liboliviamodem.a`) linked by `dan/jf8call-fork`.

## CodeWizard PR Workflow

1. Never commit or push directly to `main`; use an isolated `feat/*`, `fix/*`, or `docs/*` branch targeting `dan/olivia-modem-fork` `main` (upstream mirror: `codewizard/olivia-modem`).
2. Keep the library strictly dependency-free (C++20 standard library + `libm`).
3. Sync worktree via `codewizard_pull_repository`, open a PR, wait for `codewizard/code-review` (`APPROVED`) + `codewizard/pipeline` (`success`), and merge via `rebase-merge`.
