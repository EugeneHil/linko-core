#!/usr/bin/env bash
# Local CI for linko-core: configure + build + ctest on the host preset,
# clang-format and clang-tidy checks, and on macOS an llvm-cov coverage
# report. Usage: scripts/ci-local.sh [preset]
# (default preset: macos on macOS, linux elsewhere)
set -euo pipefail

repo=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd -P)
cd "$repo"

# clang-format, clang-tidy and the llvm-cov tools come from Homebrew LLVM
# when they are not already on PATH.
if ! command -v clang-format >/dev/null 2>&1 && [ -d /opt/homebrew/opt/llvm/bin ]; then
  PATH="/opt/homebrew/opt/llvm/bin:$PATH"
  export PATH
fi

preset=${1:-}
if [ -z "$preset" ]; then
  case "$(uname -s)" in
    Darwin) preset=macos ;;
    *) preset=linux ;;
  esac
fi

echo "== configure, build, test (preset: $preset)"
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"

# Every C++ file we track must live in include/, src/ or tests/.
sources=()
while IFS= read -r f; do
  sources+=("$f")
done < <(find include src tests -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null | sort)

echo "== clang-format check"
if [ "${#sources[@]}" -gt 0 ]; then
  clang-format --dry-run --Werror "${sources[@]}"
fi

echo "== clang-tidy check"
if [ "${#sources[@]}" -gt 0 ]; then
  clang-tidy -p "build/$preset" "${sources[@]}"
fi

if [ "$(uname -s)" = "Darwin" ]; then
  echo "== coverage report (llvm-cov)"
  covdir=build/coverage
  # The profiling runtime and the llvm-cov tools must come from the same LLVM
  # installation: build the coverage preset with the Clang that ships next to
  # llvm-profdata (Homebrew LLVM), not with whatever `c++` resolves to.
  profdata_bin=$(command -v llvm-profdata || true)
  if [ -z "$profdata_bin" ] && [ -x /opt/homebrew/opt/llvm/bin/llvm-profdata ]; then
    profdata_bin=/opt/homebrew/opt/llvm/bin/llvm-profdata
  fi
  if [ -z "$profdata_bin" ]; then
    echo "ci-local: llvm-profdata not found (install Homebrew LLVM)" >&2
    exit 1
  fi
  llvmbin=${profdata_bin%/llvm-profdata}
  # Re-configure from scratch if the build tree cached a different compiler.
  cached_cxx=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$covdir/CMakeCache.txt" 2>/dev/null || true)
  if [ -n "$cached_cxx" ] && [ "$cached_cxx" != "$llvmbin/clang++" ]; then
    rm -rf "$covdir"
  fi
  CC="$llvmbin/clang" CXX="$llvmbin/clang++" cmake --preset coverage
  cmake --build --preset coverage
  rm -f "$covdir"/*.profraw "$covdir"/coverage.profdata
  ctest --preset coverage
  "$profdata_bin" merge -sparse "$covdir"/*.profraw -o "$covdir/coverage.profdata"
  exes=()
  while IFS= read -r f; do
    exes+=("$f")
  done < <(find "$covdir/tests" -type f -perm -111 2>/dev/null | sort)
  if [ "${#exes[@]}" -gt 0 ]; then
    cov_args=("${exes[0]}")
    for exe in "${exes[@]:1}"; do
      cov_args+=(-object "$exe")
    done
    cov_sources=()
    for dir in src include tests; do
      [ -d "$repo/$dir" ] && cov_sources+=("$repo/$dir")
    done
    "$llvmbin/llvm-cov" report "${cov_args[@]}" \
      -instr-profile="$covdir/coverage.profdata" "${cov_sources[@]}"
  fi
else
  echo "== coverage report: skipped (macOS only)"
fi

echo "ci-local: OK"
