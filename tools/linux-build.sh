#!/usr/bin/env bash
# =============================================================================
#  Flopster — Linux Build Script
#  Builds VST3 + Standalone in Release (or Debug with --debug flag)
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
JOBS=$(nproc 2>/dev/null || echo 4)

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
echo -e "${BOLD}${CYAN}║   🐧  Flopster Build Script (Linux)      ║${NC}"
echo -e "${BOLD}${CYAN}║   by Shiru & Resonaura                   ║${NC}"
echo -e "${BOLD}${CYAN}╚══════════════════════════════════════════╝${NC}"
echo ""
info "Build type : ${BOLD}${BUILD_TYPE}${NC}"
info "Build dir  : ${BUILD_DIR}"
info "Jobs       : ${JOBS}"
echo ""

# ── Prerequisites ─────────────────────────────────────────────────────────────
if ! command -v cmake &>/dev/null; then
  die "cmake not found.\n  Ubuntu/Debian:  sudo apt-get install cmake\n  Fedora:         sudo dnf install cmake"
fi
ok "cmake: $(cmake --version | head -1)"

if ! command -v ninja &>/dev/null; then
  die "ninja not found.\n  Ubuntu/Debian:  sudo apt-get install ninja-build\n  Fedora:         sudo dnf install ninja-build"
fi
ok "ninja: $(ninja --version)"

if ! command -v pkg-config &>/dev/null; then
  die "pkg-config not found.\n  Ubuntu/Debian:  sudo apt-get install pkg-config\n  Fedora:         sudo dnf install pkgconf-pkg-config"
fi
ok "pkg-config: $(pkg-config --version)"

if [ ! -d "$ROOT/JUCE/modules" ]; then
  warn "JUCE not found — cloning JUCE 8.0.7…"
  git clone --depth 1 --branch 8.0.7 \
    https://github.com/juce-framework/JUCE.git "$ROOT/JUCE" \
    || die "Failed to clone JUCE"
  ok "JUCE cloned"
else
  ok "JUCE: $ROOT/JUCE"
fi
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
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
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

# ── Sync compile_commands.json to repo root ───────────────────────────────────
if [ -f "$BUILD_DIR/compile_commands.json" ]; then
  cp "$BUILD_DIR/compile_commands.json" "$ROOT/compile_commands.json"
  ok "compile_commands.json synced to repo root"
fi

# ── Verify artefacts ──────────────────────────────────────────────────────────
info "Verifying artefacts…"
BASE="$BUILD_DIR/Flopster_artefacts/${BUILD_TYPE}"
ALL_OK=1

# VST3 — binary lives in an arch-specific subdirectory
case "$(uname -m)" in
  x86_64)  VST3_BIN="$BASE/VST3/Flopster.vst3/Contents/x86_64-linux/Flopster.so" ;;
  aarch64) VST3_BIN="$BASE/VST3/Flopster.vst3/Contents/aarch64-linux/Flopster.so" ;;
  *)       VST3_BIN="$BASE/VST3/Flopster.vst3/Contents/$(uname -m)-linux/Flopster.so" ;;
esac

if [ -d "$BASE/VST3/Flopster.vst3" ] && [ -f "$VST3_BIN" ]; then
  ok "VST3/Flopster.vst3"
else
  warn "Missing or incomplete: $BASE/VST3/Flopster.vst3"
  ALL_OK=0
fi

if [ -f "$BASE/Standalone/flopster" ]; then
  ok "Standalone/flopster"
else
  warn "Missing: $BASE/Standalone/flopster"
  ALL_OK=0
fi
echo ""

if [ "$ALL_OK" -eq 1 ]; then
  echo -e "${GREEN}${BOLD}Build complete!${NC}"
else
  warn "Some artefacts missing — check build output above"
fi

echo ""
echo -e "  VST3      →  ${BOLD}${BASE}/VST3/Flopster.vst3${NC}"
echo -e "  Standalone→  ${BOLD}${BASE}/Standalone/flopster${NC}"
echo ""
echo -e "  Run ${BOLD}./tools/linux-install.sh${NC} to install into the system."
echo ""
