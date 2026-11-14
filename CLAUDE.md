# CLAUDE.md

Linko: a private 1:1 messenger for iPhone and Mac with audio/video calls and file transfer. Design: `linko-apple/docs/superpowers/specs/2026-10-25-linko-design.md`.

## Rules (non-negotiable)

- **TDD:** write the failing test first, watch it fail, then implement. No production code without a test that failed first.
- **Branches:** never commit or push to `main` or `develop`. Work in `feature/LNK-<n>-<slug>`, `fix/…`, `chore/…`, `docs/…`, `spike/…`. PRs target `develop`; merge with `gh pr merge` only after checks are green and the owner approved. Releases: PR `develop → main` + tag `vX.Y.Z`.
- **Commits:** Conventional Commits (`feat:`, `fix:`, `chore:`, `docs:`, `test:`, `refactor:`, `ci:`, `build:`, `perf:`). Never `--no-verify`, never skip hooks, never force push.
- **Secrets:** never read, write or commit `.env*` (except `.env.example`), `*.pem`, `*.key`, `*.p12`, `*.p8`, `*.cer`, `*.mobileprovision`, `config.local.json`. Configuration goes into `*.example*` templates with placeholders. A private key never leaves the host it belongs to.
- **Logging:** never log tokens, invite codes, `Authorization` values, message bodies, SDP or ICE credentials.
- **Dependencies:** no new dependency without an ADR in `docs/adr/`; versions pinned exactly.
- **Swift:** Swift Concurrency only (async/await, actors, `AsyncStream`/`AsyncSequence`, `Task`); no GCD, `OperationQueue`, Combine or completion handlers in our code. Callback and delegate APIs (WebRTC, CallKit, `URLSessionWebSocketTask`, `NWPathMonitor`, `AVAudioSession` notifications) are wrapped once at the boundary. Where an Apple API demands a synchronous answer (a CallKit action must be fulfilled or failed), the boundary adapter does exactly that and nothing more. No force unwraps, no `try!`.
- **Warnings are errors** in every language.
- **Docs:** English.

## Guard hook

`tooling/claude/guard.py` runs before every Read/Edit/Write/Bash call and enforces the secret and branch rules above. Do not disable it. Tests: `python3 -I -B -m unittest discover -s tooling/claude`.

## linko-core stack

- Pure C++17 (no platform code), CMake ≥ 3.28 with presets. Warnings are errors: GCC/Clang `-Wall -Wextra -Wpedantic -Werror`, MSVC `/W4 /WX`.
- Dependencies come via CMake `FetchContent` pinned to release tarballs (`URL` + `URL_HASH SHA256=...`) in `cmake/Dependencies.cmake`: GoogleTest `v1.18.0`, nlohmann/json `v3.12.0`. Decision, tags and hashes: ADR-0002 (`linko-apple/docs/adr/0002-core-dependencies.md`); upgrades go through a PR + ADR note.
- Build and test (presets: `macos`, `windows`, `linux` dev-only, `coverage` macOS + llvm-cov):
  `cmake --preset macos && cmake --build --preset macos && ctest --preset macos`
- Coverage (macOS): `scripts/ci-local.sh` builds the `coverage` preset with the Clang from the same LLVM as `llvm-profdata`/`llvm-cov` (Homebrew LLVM), runs the tests under `LLVM_PROFILE_FILE`, merges with `llvm-profdata merge -sparse` and reports with `llvm-cov report`. When running the steps by hand, keep the compiler and the llvm-cov tools from the same LLVM installation, or the profile merge fails.
- One command for everything: `scripts/ci-local.sh` — configure + build + ctest on the host preset, `clang-format`/`clang-tidy` checks, coverage report on macOS.
- C++ style: `.clang-format` (LLVM base, 4-space indent; check with `clang-format --dry-run --Werror <files>`); lint: `.clang-tidy` (`bugprone-*`, `modernize-*`, `readability-*`, `performance-*`, findings are errors).
- Layout: public headers in `include/linko/`, sources in `src/`, GoogleTest suites in `tests/` registered with `linko_add_test(<name> <sources…>)`.
