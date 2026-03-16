#!/usr/bin/env bash
# =============================================================================
#  Flopster — Universal macOS Build Script
#  Builds VST3 + AU + Standalone in Release (or Debug with --debug flag)
#  by Shiru & Resonaura
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_RELEASE="$ROOT/build"
BUILD_DEBUG="$ROOT/build-debug"

# ── Colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${CYAN}[build]${NC} $*"; }
ok()    { echo -e "${GREEN}  ✅  $*${NC}"; }
warn()  { echo -e "${YELLOW}  ⚠️   $*${NC}"; }
die()   { echo -e "${RED}  ❌  $*${NC}"; exit 1; }

# ── Defaults ──────────────────────────────────────────────────────────────────
BUILD_TYPE=Release
BUILD_DIR="$BUILD_RELEASE"
REBUILD=0
JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# ── Argument parsing ──────────────────────────────────────────────────────────
for arg in "$@"; do
  case "$arg" in
    --debug|-d)   BUILD_TYPE=Debug;   BUILD_DIR="$BUILD_DEBUG" ;;
    --release|-r) BUILD_TYPE=Release; BUILD_DIR="$BUILD_RELEASE" ;;
    --rebuild)    REBUILD=1 ;;
    --help|-h)
      echo ""
      echo "  Usage: $0 [--debug] [--release] [--rebuild] [--help]"
      echo ""
      echo "    --debug     Build Debug configuration"
      echo "    --release   Build Release configuration (default)"
      echo "    --rebuild   Remove build directory and rebuild from scratch"
      echo "    --help      Show this help"
      echo ""
      exit 0
      ;;
    *) warn "Unknown flag: $arg (ignored)" ;;
  esac
done

# ── Banner ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}${CYAN}╔══════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${CYAN}║   🖥️  Flopster Build Script               ║${NC}"
echo -e "${BOLD}${CYAN}║   by Shiru & Resonaura                   ║${NC}"
echo -e "${BOLD}${CYAN}╚══════════════════════════════════════════╝${NC}"
echo ""
info "Build type : ${BOLD}${BUILD_TYPE}${NC}"
info "Build dir  : ${BUILD_DIR}"
info "Jobs       : ${JOBS}"
echo ""

# ── Prerequisites ─────────────────────────────────────────────────────────────
if ! xcode-select -p &>/dev/null; then
  die "Xcode Command Line Tools not found.\nRun: xcode-select --install"
fi
ok "Xcode CLT: $(xcode-select -p)"

if ! command -v cmake &>/dev/null; then
  die "cmake not found. Install with: brew install cmake"
fi
ok "cmake: $(cmake --version | head -1)"

if ! command -v ninja &>/dev/null; then
  warn "ninja not found — attempting brew install ninja…"
  if command -v brew &>/dev/null; then
    brew install ninja || die "Failed to install ninja via Homebrew"
  else
    die "ninja not found and Homebrew unavailable.\nInstall Homebrew first: https://brew.sh"
  fi
fi
ok "ninja: $(ninja --version)"

if [ ! -d "$ROOT/JUCE/modules" ]; then
  warn "JUCE not found — cloning JUCE 8.0.7…"
  git clone --depth 1 --branch 8.0.7 \
    https://github.com/juce-framework/JUCE.git "$ROOT/JUCE" \
    || die "Failed to clone JUCE"
  ok "JUCE cloned"
else
  ok "JUCE: $ROOT/JUCE"
fi

CC=$(xcrun --find clang   2>/dev/null || echo clang)
CXX=$(xcrun --find clang++ 2>/dev/null || echo clang++)
ok "Compiler: $CXX"
echo ""

# ── Clean if rebuild ──────────────────────────────────────────────────────────
if [ "$REBUILD" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
  info "🗑️  Removing existing build directory…"
  rm -rf "$BUILD_DIR"
  ok "Removed $BUILD_DIR"
fi

# ── CMake configure ───────────────────────────────────────────────────────────
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  info "🔧 Configuring with CMake…"
  mkdir -p "$BUILD_DIR"
  cmake -S "$ROOT" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    2>&1 | sed 's/^/    /' \
    || die "CMake configuration failed"
  ok "CMake configuration done"
else
  info "CMake already configured (use --rebuild to reconfigure)"
fi
echo ""

# ── Build ─────────────────────────────────────────────────────────────────────
info "🔨 Building Flopster (${BUILD_TYPE}, ${JOBS} cores)…"
echo ""
ninja -C "$BUILD_DIR" -j"$JOBS" 2>&1 \
  | grep -E "^\[|error:|warning:|Linking|Building CXX object CMakeFiles/Flopster" \
  || die "Build failed — for full output run:\n  ninja -C $BUILD_DIR"
echo ""

# ── Verify artefacts ──────────────────────────────────────────────────────────
info "Verifying artefacts…"
BASE="$BUILD_DIR/Flopster_artefacts/${BUILD_TYPE}"
ALL_OK=1
for A in "VST3/Flopster.vst3" "AU/Flopster.component" "Standalone/Flopster.app"; do
  if [ -e "$BASE/$A" ]; then
    ok "$A"
  else
    warn "Missing: $BASE/$A"
    ALL_OK=0
  fi
done
echo ""

if [ "$ALL_OK" -eq 1 ]; then
  echo -e "${GREEN}${BOLD}Build complete!${NC}"
else
  warn "Some artefacts missing — check build output above"
fi

echo ""
echo -e "  VST3      →  ${BOLD}${BASE}/VST3/Flopster.vst3${NC}"
echo -e "  AU        →  ${BOLD}${BASE}/AU/Flopster.component${NC}"
echo -e "  Standalone→  ${BOLD}${BASE}/Standalone/Flopster.app${NC}"
echo ""
echo -e "  Run ${BOLD}./tools/mac-install.sh${NC} to install into the system."
echo ""
