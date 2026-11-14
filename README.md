# linko-core

Cross-platform C++17 core of **Linko**, a private 1:1 messenger with audio/video calls and file transfer: the signaling and chat protocol, the call state machine, file chunking and message-sync rules, shared by the server and the iOS/macOS apps. Also hosts the design tokens and the shared repository tooling (`tooling/`).

Everything here is pure value types and pure functions — no I/O, no clocks, no sockets. Parsing and validation never throw; the one exception source is `serialize`, which throws only on a hand-built envelope with invalid UTF-8 (see `include/linko/envelope.hpp`). The hosts (apps and server) move bytes and feed events back in.

- Design spec: [linko-apple `docs/superpowers/specs/2026-10-25-linko-design.md`](https://github.com/EugeneHil/linko-apple/blob/develop/docs/superpowers/specs/2026-10-25-linko-design.md) (§4.2 LinkoCore, §4.8 design system, §7 quality gates)
- Architecture decisions: [linko-apple `docs/adr/`](https://github.com/EugeneHil/linko-apple/tree/develop/docs/adr) — [ADR-0002](https://github.com/EugeneHil/linko-apple/blob/develop/docs/adr/0002-core-dependencies.md) records the dependency set: GoogleTest `v1.18.0` and nlohmann/json `v3.12.0`, pinned as release tarballs with SHA-256 in `cmake/Dependencies.cmake`.

## Module map

Public headers in `include/linko/`, implementations in `src/`, GoogleTest suites in `tests/` (registered with `linko_add_test`; run `ctest --preset macos` for the current count).

| Module | Files | Provides |
|---|---|---|
| Envelope | `include/linko/envelope.hpp`, `src/envelope.cpp` | Protocol v1 envelope: parse/serialize with byte-level limits (64 KiB envelope, 32 KiB SDP, nesting depth 16), strict UTF-8 and JSON-path validation errors; unknown types rejected, unknown fields ignored |
| Messages | `include/linko/messages.hpp`, `src/messages.cpp` | Typed bodies for all four message families (session, call, chat, file): `toBody()`/`fromBody()` plus the 25 `kType*` wire names behind `kKnownTypes` |
| Call state machine | `include/linko/call_state_machine.hpp`, `src/call_state_machine.cpp` | One call's lifecycle as a pure transition table `(CallState, CallEvent) → Transition`: glare resolution, ring/connect/reconnect timeouts arriving as events, ICE restart, sends and CallKit reports leaving as effects |
| File transfer | `include/linko/file_transfer.hpp`, `src/file_transfer.cpp`, `include/linko/sha256.hpp`, `src/sha256.cpp` | In-call file transfer: manifest, chunk framing, reassembly with duplicate/gap handling, resume from the last acknowledged chunk, SHA-256 integrity (vendored FIPS 180-4 implementation — no new dependency) |
| Sync | `include/linko/sync.hpp`, `src/sync.cpp` | Conversation history: server `seq` ordering, idempotent sends by client id, "everything after seq N" queries, monotonic receipts (read implies delivered, receipts never regress) |
| Design tokens | `design-tokens/` | `tokens.json` single source of truth and the stdlib-only Python generator that emits `Tokens.swift` deterministically; see [`design-tokens/README.md`](design-tokens/README.md) |
| Tooling | `tooling/` | Shared repository tooling: Claude guard hook, GitHub bootstrap, rulesets |

Roadmap: this map grows in Plans 3–4 — the server and the apps consume this core, and later plans add the transport-facing pieces (server protocol handling, app-side adapters) around the same pure core.

## Build, test, coverage

CMake ≥ 3.28 with presets (`macos`, `windows`, `linux` — dev only, `coverage` — macOS + llvm-cov). Warnings are errors everywhere (`-Wall -Wextra -Wpedantic -Werror`, MSVC `/W4 /WX`).

```sh
cmake --preset macos && cmake --build --preset macos && ctest --preset macos   # macOS
cmake --preset windows && cmake --build --preset windows && ctest --preset windows  # Windows (auto-detected VS)
```

One command for everything the CI checks:

```sh
scripts/ci-local.sh   # configure + build + ctest, clang-format, clang-tidy,
                      # design-tokens check + tests, llvm-cov report (macOS)
```

Coverage on macOS (the script keeps the compiler and the llvm-cov tools from the same LLVM installation — mixing them breaks the profile merge):

```sh
cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage
llvm-profdata merge -sparse build/coverage/*.profraw -o build/coverage/coverage.profdata
llvm-cov report build/coverage/tests/<suite> -instr-profile=build/coverage/coverage.profdata src
```

## CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs only on pull requests targeting `develop`/`main` (spec §8: macOS and Windows runners are budgeted, never spent on every push; a `changes` job skips the build jobs entirely when a PR touches neither C++/CMake nor the design tokens):

| Job | Runner | What it does |
|---|---|---|
| `macos` | macOS | configure + build + ctest via the `macos` preset, ccache cache |
| `windows` | Windows | configure + build + ctest via the `windows` preset, build-tree cache |
| `coverage` | macOS | `coverage` preset, llvm-cov line coverage of `linko_core`; **fails under 90 %** (spec §7) |
| `lint` | Ubuntu | `clang-format --dry-run --Werror` on all sources, `clang-tidy` (warnings are errors) on changed `.cpp` files — headers via their including TU |
| `tokens` | Ubuntu | `design-tokens/generate.py --check` + the token unittests |

Secret scanning runs separately for every PR and push in [`.github/workflows/gitleaks.yml`](.github/workflows/gitleaks.yml).
