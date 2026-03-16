#!/usr/bin/env bash
# =============================================================================
#  Flopster — macOS Installer
#  Builds (if needed), installs VST3 / AU / Standalone, strips Gatekeeper
#  quarantine, ad-hoc codesigns, validates AU, and copies bundled resources.
#  by Shiru & Resonaura
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/build"
ASSETS="$SCRIPT_DIR/assets"
SAMPLES="$SCRIPT_DIR/samples"

ARTEFACTS="$BUILD/Flopster_artefacts/Release"
VST3_SRC="$ARTEFACTS/VST3/Flopster.vst3"
AU_SRC="$ARTEFACTS/AU/Flopster.component"
APP_SRC="$ARTEFACTS/Standalone/Flopster.app"

VST3_DST="$HOME/Library/Audio/Plug-Ins/VST3/Flopster.vst3"
AU_DST="$HOME/Library/Audio/Plug-Ins/Components/Flopster.component"
APP_DST="$HOME/Applications/Flopster.app"

# ── Colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; DIM='\033[2m'; NC='\033[0m'

info()  { echo -e "${CYN}${BLD}  $*${NC}"; }
ok()    { echo -e "${GRN}  ✅  $*${NC}"; }
warn()  { echo -e "${YLW}  ⚠️   $*${NC}"; }
die()   { echo -e "${RED}  ❌  $*${NC}" >&2; exit 1; }
step()  { echo -e "\n${BLD}${YLW}$*${NC}"; }
ruler() { echo -e "${DIM}  ──────────────────────────────────────────────────${NC}"; }

# ── Banner ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   🎹  Flopster Plugin Installer  v1.21       ║${NC}"
echo -e "${CYN}${BLD}║       by Shiru & Resonaura                   ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""

# ── Parse flags ───────────────────────────────────────────────────────────────
REBUILD=0
for arg in "$@"; do
    case "$arg" in
        --rebuild|-r) REBUILD=1 ;;
        --help|-h)
            echo "  Usage: $0 [--rebuild]"
            echo "    --rebuild   Force a clean rebuild even if artefacts exist."
            exit 0
            ;;
        *) warn "Unknown argument: $arg  (ignoring)" ;;
    esac
done

# ── Create ~/Applications if needed ──────────────────────────────────────────
mkdir -p "$HOME/Applications"

# ── 1. Prerequisites ──────────────────────────────────────────────────────────
step "🔨 Checking prerequisites..."
ruler

if ! xcode-select -p &>/dev/null; then
    die "Xcode Command Line Tools not found.\nInstall with:  xcode-select --install"
fi
ok "Xcode CLT: $(xcode-select -p)"

if ! command -v cmake &>/dev/null; then
    die "cmake not found.\nInstall with:  brew install cmake"
fi
ok "cmake $(cmake --version | head -1 | awk '{print $3}')"

if ! command -v ninja &>/dev/null; then
    warn "ninja not found — attempting to install via Homebrew..."
    if ! command -v brew &>/dev/null; then
        die "Homebrew not found. Install it from https://brew.sh/ then re-run."
    fi
    brew install ninja || die "Failed to install ninja via Homebrew."
    ok "ninja installed"
else
    ok "ninja $(ninja --version)"
fi

if [ ! -d "$SCRIPT_DIR/JUCE/modules" ]; then
    warn "JUCE not found — cloning JUCE 8.0.7…"
    git clone --depth 1 --branch 8.0.7 \
        https://github.com/juce-framework/JUCE.git "$SCRIPT_DIR/JUCE" \
        || die "Failed to clone JUCE"
    ok "JUCE cloned"
else
    ok "JUCE found"
fi

# ── 2. Build if needed ────────────────────────────────────────────────────────
step "🔨 Build check..."
ruler

ARTEFACT_BINARY="$VST3_SRC/Contents/MacOS/Flopster"

if [ "$REBUILD" -eq 1 ] || [ ! -f "$ARTEFACT_BINARY" ]; then
    if [ "$REBUILD" -eq 1 ] && [ -d "$BUILD" ]; then
        info "🗑️  --rebuild: wiping old build directory..."
        rm -rf "$BUILD"
        ok "Old build removed."
    fi

    CC="$(xcrun --find clang)"
    CXX="$(xcrun --find clang++)"
    JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

    info "Configuring…"
    mkdir -p "$BUILD"
    cmake -S "$SCRIPT_DIR" -B "$BUILD" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        "-DCMAKE_C_COMPILER=$CC" \
        "-DCMAKE_CXX_COMPILER=$CXX" \
        2>&1 | sed 's/^/    /' \
        || die "CMake configuration failed."
    ok "Configured."

    info "Building ($JOBS cores)…"
    ninja -C "$BUILD" -j"$JOBS" \
        2>&1 | grep -E "^\[|error:|Linking" \
        || die "Build failed. Run  ninja -C $BUILD  for full output."
    ok "Build succeeded."
else
    ok "Release artefacts already present — skipping build.  (Pass --rebuild to force.)"
fi

# ── 3. Verify sources ─────────────────────────────────────────────────────────
step "📦 Verifying source artefacts..."
ruler

[ -d "$VST3_SRC"  ] || die "VST3 artefact not found: $VST3_SRC"
[ -d "$AU_SRC"    ] || die "AU artefact not found: $AU_SRC"
[ -d "$APP_SRC"   ] || die "Standalone artefact not found: $APP_SRC"
[ -f "$ASSETS/back.bmp" ] || die "back.bmp not found in assets/"
[ -f "$ASSETS/char.bmp" ] || die "char.bmp not found in assets/"
[ -d "$SAMPLES"   ] || die "samples/ directory not found"

ok "Found: Flopster.vst3"
ok "Found: Flopster.component"
ok "Found: Flopster.app"
ok "Assets and samples directory verified."

# ── helpers ───────────────────────────────────────────────────────────────────
copy_resources() {
    local dest="$1"
    mkdir -p "$dest"
    cp "$ASSETS/back.bmp" "$dest/"
    cp "$ASSETS/char.bmp" "$dest/"
    rm -rf "$dest/samples"
    cp -R "$SAMPLES" "$dest/samples"
}

strip_quarantine() {
    xattr -rd com.apple.quarantine "$1" 2>/dev/null || true
    ok "Quarantine flag removed from $(basename "$1")"
}

adhoc_sign() {
    codesign --force --deep --sign - "$1" 2>/dev/null || true
    ok "Ad-hoc codesign applied to $(basename "$1")"
}

# ── 4. Install VST3 ───────────────────────────────────────────────────────────
step "🎹 Installing VST3 → $VST3_DST"
ruler
mkdir -p "$(dirname "$VST3_DST")"
rm -rf "$VST3_DST"
cp -R "$VST3_SRC" "$VST3_DST"
ok "Bundle copied."

copy_resources "$VST3_DST/Contents/Resources"
ok "Resources copied."

strip_quarantine "$VST3_DST"
adhoc_sign       "$VST3_DST"

# ── 5. Install AU ─────────────────────────────────────────────────────────────
step "🎹 Installing AU → $AU_DST"
ruler
mkdir -p "$(dirname "$AU_DST")"
rm -rf "$AU_DST"
cp -R "$AU_SRC" "$AU_DST"
ok "Bundle copied."

copy_resources "$AU_DST/Contents/Resources"
ok "Resources copied."

strip_quarantine "$AU_DST"
adhoc_sign       "$AU_DST"

# ── 6. Install Standalone ─────────────────────────────────────────────────────
step "🎹 Installing Standalone → $APP_DST"
ruler
rm -rf "$APP_DST"
cp -R "$APP_SRC" "$APP_DST"
ok "Bundle copied."

# Populate both Resources/ (primary lookup) and MacOS/ (fallback)
copy_resources "$APP_DST/Contents/Resources"
ok "Resources copied (Contents/Resources)."

mkdir -p "$APP_DST/Contents/MacOS"
cp "$ASSETS/back.bmp" "$APP_DST/Contents/MacOS/"
cp "$ASSETS/char.bmp" "$APP_DST/Contents/MacOS/"
rm -rf "$APP_DST/Contents/MacOS/samples"
cp -R "$SAMPLES" "$APP_DST/Contents/MacOS/samples"
ok "Resources copied (Contents/MacOS fallback)."

strip_quarantine "$APP_DST"
adhoc_sign       "$APP_DST"

# ── 7. AU validation ──────────────────────────────────────────────────────────
step "🔬 Validating AU component..."
ruler
info "Running:  auval -v aumu Flps Shru"
echo ""

AU_RESULT="$(auval -v aumu Flps Shru 2>&1)"
echo "$AU_RESULT" | sed 's/^/    /'
echo ""

if echo "$AU_RESULT" | grep -q "AU VALIDATION SUCCEEDED"; then
    ok "AU validation  ✅  PASSED"
else
    warn "AU validation returned unexpected output — check above."
fi

# ── 8. Summary ────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   ✅  Installation complete!                 ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  VST3        →  ${BLD}$VST3_DST${NC}"
echo -e "  AU          →  ${BLD}$AU_DST${NC}"
echo -e "  Standalone  →  ${BLD}$APP_DST${NC}"
echo ""
echo -e "  Next steps:"
echo -e "  • Restart your DAW or trigger a plugin rescan."
echo -e "  • Logic Pro / GarageBand:  Preferences → Plug-in Manager → Reset & Rescan"
echo -e "  • Ableton Live:            Options → Preferences → Plug-Ins → Rescan"
echo -e "  • Launch standalone:       open \"$APP_DST\""
echo ""
