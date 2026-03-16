#!/usr/bin/env bash
# =============================================================================
#  Flopster — Linux AppImage Builder
#
#  Produces a portable  Flopster-<version>-<arch>.AppImage  that runs on any
#  modern Linux without installation.  appimagetool is downloaded automatically
#  from GitHub releases if it is not already in PATH.
#
#  Usage:
#    ./tools/linux-appimage.sh                  # build then create AppImage
#    ./tools/linux-appimage.sh --rebuild        # force clean rebuild first
#    ./tools/linux-appimage.sh --no-build       # skip build, use existing artefacts
#    ./tools/linux-appimage.sh --out <dir>      # write AppImage to <dir>  (default: dist/)
#    ./tools/linux-appimage.sh --help
#
#  Requirements:
#    cmake, ninja  (appimagetool is downloaded automatically if missing)
#
#  by Shiru & Resonaura
# =============================================================================
set -euo pipefail

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD="$ROOT/build"
ASSETS="$ROOT/assets"
SAMPLES="$ROOT/samples"
DIST="$ROOT/dist"

ARTEFACTS="$BUILD/Flopster_artefacts/Release"
BIN_SRC="$ARTEFACTS/Standalone/flopster"

APPDIR="$ROOT/.appimage_stage/AppDir"
TOOLS_DIR="$ROOT/.appimage_stage/tools"

VERSION="$(node -e "process.stdout.write(require('$ROOT/package.json').version)")"

# ── Architecture detection ────────────────────────────────────────────────────
UNAME_M="$(uname -m)"
case "$UNAME_M" in
    x86_64)  APPIMAGE_ARCH="x86_64"  ; TOOL_ARCH="x86_64"  ;;
    aarch64) APPIMAGE_ARCH="aarch64" ; TOOL_ARCH="aarch64"  ;;
    armv7l)  APPIMAGE_ARCH="armhf"   ; TOOL_ARCH="armhf"    ;;
    *)       APPIMAGE_ARCH="$UNAME_M"; TOOL_ARCH="$UNAME_M" ;;
esac

APPIMAGE_NAME="Flopster-${VERSION}-${APPIMAGE_ARCH}.AppImage"

# ── Colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; DIM='\033[2m'; NC='\033[0m'

info()  { echo -e "${CYN}${BLD}  →  $*${NC}"; }
ok()    { echo -e "${GRN}  ✅  $*${NC}"; }
warn()  { echo -e "${YLW}  ⚠️   $*${NC}"; }
die()   { echo -e "${RED}  ❌  $*${NC}" >&2; exit 1; }
step()  { echo -e "\n${BLD}${YLW}── $* ──${NC}"; }
ruler() { echo -e "${DIM}  ────────────────────────────────────────────────${NC}"; }

# ── Parse flags ───────────────────────────────────────────────────────────────
REBUILD=0
NO_BUILD=0
OUT_DIR="$DIST"

show_help() {
    echo ""
    echo "  Usage: $0 [options]"
    echo ""
    echo "  Options:"
    echo "    --rebuild        Force a clean CMake rebuild before packaging."
    echo "    --no-build       Skip the build step entirely (use existing artefacts)."
    echo "    --out <dir>      Write the final AppImage to <dir>  (default: dist/)"
    echo "    --help, -h       Show this help."
    echo ""
    echo "  Output:"
    echo "    dist/${APPIMAGE_NAME}"
    echo ""
    echo "  The AppImage is self-contained and requires no installation."
    echo "  Just make it executable and run it:"
    echo "    chmod +x dist/${APPIMAGE_NAME}"
    echo "    ./dist/${APPIMAGE_NAME}"
    echo ""
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rebuild)   REBUILD=1;    shift ;;
        --no-build)  NO_BUILD=1;   shift ;;
        --out)
            [[ -z "${2:-}" ]] && die "--out requires a directory argument"
            OUT_DIR="$2"; shift 2 ;;
        --out=*)     OUT_DIR="${1#--out=}"; shift ;;
        --help|-h)   show_help ;;
        *) warn "Unknown argument: $1  (ignoring)"; shift ;;
    esac
done

[[ $REBUILD -eq 1 && $NO_BUILD -eq 1 ]] && die "--rebuild and --no-build are mutually exclusive."

# ── Banner ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   🗜️   Flopster AppImage Builder              ║${NC}"
echo -e "${CYN}${BLD}║       by Shiru & Resonaura                   ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
info "Version:      $VERSION"
info "Architecture: $APPIMAGE_ARCH"
info "Output:       $OUT_DIR/$APPIMAGE_NAME"

# ── 1. Prerequisites ──────────────────────────────────────────────────────────
step "Checking prerequisites"
ruler

for tool in cmake ninja; do
    if ! command -v "$tool" &>/dev/null; then
        case "$tool" in
            cmake) die "cmake not found.\n  Ubuntu/Debian:  sudo apt-get install cmake\n  Fedora:         sudo dnf install cmake" ;;
            ninja) die "ninja not found.\n  Ubuntu/Debian:  sudo apt-get install ninja-build\n  Fedora:         sudo dnf install ninja-build" ;;
        esac
    fi
    ok "$tool found"
done

# ── Locate or download appimagetool ──────────────────────────────────────────
APPIMAGETOOL=""

if command -v appimagetool &>/dev/null; then
    APPIMAGETOOL="$(command -v appimagetool)"
    ok "appimagetool found in PATH: $APPIMAGETOOL"
else
    warn "appimagetool not found in PATH — will download automatically."

    if ! command -v curl &>/dev/null && ! command -v wget &>/dev/null; then
        die "Neither curl nor wget is available. Install one to allow appimagetool download.\n  Ubuntu/Debian:  sudo apt-get install curl\n  Fedora:         sudo dnf install curl"
    fi

    mkdir -p "$TOOLS_DIR"
    APPIMAGETOOL="$TOOLS_DIR/appimagetool-${TOOL_ARCH}.AppImage"

    if [ ! -f "$APPIMAGETOOL" ]; then
        TOOL_URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${TOOL_ARCH}.AppImage"
        info "Downloading appimagetool from:"
        info "  $TOOL_URL"

        if command -v curl &>/dev/null; then
            curl -fsSL --progress-bar -o "$APPIMAGETOOL" "$TOOL_URL" \
                || die "Download failed. Check your internet connection or download manually:\n  $TOOL_URL"
        else
            wget -q --show-progress -O "$APPIMAGETOOL" "$TOOL_URL" \
                || die "Download failed. Check your internet connection or download manually:\n  $TOOL_URL"
        fi

        chmod +x "$APPIMAGETOOL"
        ok "appimagetool downloaded: $APPIMAGETOOL"
    else
        ok "appimagetool already cached: $APPIMAGETOOL"
    fi
fi

# ── JUCE ──────────────────────────────────────────────────────────────────────
if [ ! -d "$ROOT/JUCE/modules" ]; then
    if [[ $NO_BUILD -eq 1 ]]; then
        warn "JUCE directory not found, but --no-build was passed — skipping clone."
    else
        warn "JUCE not found — cloning JUCE 8.0.7…"
        if ! command -v git &>/dev/null; then
            die "git not found. Install with:  sudo apt-get install git  or  sudo dnf install git"
        fi
        git clone --depth 1 --branch 8.0.7 \
            https://github.com/juce-framework/JUCE.git "$ROOT/JUCE" \
            || die "Failed to clone JUCE"
        ok "JUCE cloned"
    fi
else
    ok "JUCE found"
fi

# ── 2. Build ──────────────────────────────────────────────────────────────────
if [[ $NO_BUILD -eq 0 ]]; then
    step "Building Flopster"
    ruler

    # Determine the expected VST3 binary path (arch-specific subdirectory)
    case "$UNAME_M" in
        x86_64)  ARTEFACT_BINARY="$ARTEFACTS/VST3/Flopster.vst3/Contents/x86_64-linux/Flopster.so" ;;
        aarch64) ARTEFACT_BINARY="$ARTEFACTS/VST3/Flopster.vst3/Contents/aarch64-linux/Flopster.so" ;;
        *)       ARTEFACT_BINARY="$ARTEFACTS/VST3/Flopster.vst3/Contents/${UNAME_M}-linux/Flopster.so" ;;
    esac

    if [ "$REBUILD" -eq 1 ] && [ -d "$BUILD" ]; then
        info "🗑️  --rebuild: wiping old build directory..."
        rm -rf "$BUILD"
        ok "Old build removed."
    fi

    if [ "$REBUILD" -eq 1 ] || [ ! -f "$ARTEFACT_BINARY" ]; then
        JOBS="$(nproc 2>/dev/null || echo 4)"

        info "Configuring…"
        mkdir -p "$BUILD"
        cmake -S "$ROOT" -B "$BUILD" \
            -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            2>&1 | sed 's/^/    /' \
            || die "CMake configuration failed."
        ok "Configured."

        info "Building ($JOBS cores)…"
        ninja -C "$BUILD" -j"$JOBS" 2>&1 | sed 's/^/    /' \
            || die "Build failed. Run  ninja -C $BUILD  for full output."
        ok "Build succeeded."

        if [ -f "$BUILD/compile_commands.json" ]; then
            cp "$BUILD/compile_commands.json" "$ROOT/compile_commands.json"
            ok "compile_commands.json synced to repo root."
        fi
    else
        ok "Artefacts already present — skipping build.  (Pass --rebuild to force.)"
    fi
else
    step "Skipping build (--no-build)"
    ruler
    ok "Using existing artefacts in $ARTEFACTS"
fi

# ── 3. Verify source artefacts ────────────────────────────────────────────────
step "Verifying source artefacts"
ruler

[ -f "$BIN_SRC" ]         || die "Standalone binary not found: $BIN_SRC\n  Run without --no-build to trigger a build."
[ -f "$ASSETS/app.png" ]  || die "app.png not found in assets/"
[ -d "$SAMPLES" ]         || die "samples/ directory not found"

ok "Standalone binary: $BIN_SRC"
ok "Assets and samples verified."

# ── 4. Build AppDir ───────────────────────────────────────────────────────────
step "Building AppDir structure"
ruler

info "Wiping old AppDir…"
rm -rf "$ROOT/.appimage_stage/AppDir"

# Directory layout
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/flopster"

# Standalone binary
info "Copying standalone binary…"
cp "$BIN_SRC" "$APPDIR/usr/bin/flopster"
chmod 755     "$APPDIR/usr/bin/flopster"
ok "Binary copied."

# Resources (scanlines.png, samples/)
info "Copying resources…"
[[ -f "$ASSETS/scanlines.png" ]] && cp "$ASSETS/scanlines.png" "$APPDIR/usr/share/flopster/"
cp -R "$SAMPLES"      "$APPDIR/usr/share/flopster/samples"
ok "Resources copied."

# Icon — AppImage spec requires the icon at AppDir root AND in standard path
info "Copying icon…"
cp "$ASSETS/app.png" "$APPDIR/flopster.png"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$ASSETS/app.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/flopster.png"
ok "Icon copied."

# .desktop file — required at AppDir root by AppImage spec
info "Writing .desktop file…"
cat > "$APPDIR/flopster.desktop" <<'DESKTOP'
[Desktop Entry]
Name=Flopster
Comment=Floppy drive instrument
Exec=flopster
Icon=flopster
Type=Application
Categories=Audio;Music;
DESKTOP
ok ".desktop file written."

# AppRun launcher script
info "Writing AppRun…"
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
# Flopster AppImage launcher
HERE="$(dirname "$(readlink -f "$0")")"

# Make resources discoverable relative to the AppImage
export FLOPSTER_RESOURCES="$HERE/usr/share/flopster"

exec "$HERE/usr/bin/flopster" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"
ok "AppRun written."

# ── 5. Build AppImage ─────────────────────────────────────────────────────────
step "Running appimagetool"
ruler

mkdir -p "$OUT_DIR"
APPIMAGE_OUT="$OUT_DIR/$APPIMAGE_NAME"

info "Building $APPIMAGE_OUT …"

# appimagetool reads ARCH from the environment for the output filename tagging
ARCH="$APPIMAGE_ARCH" "$APPIMAGETOOL" \
    "$APPDIR" \
    "$APPIMAGE_OUT" \
    2>&1 | sed 's/^/    /' \
    || die "appimagetool failed."

chmod +x "$APPIMAGE_OUT"
ok "AppImage built: $APPIMAGE_OUT"

# ── 6. Summary ────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   ✅  AppImage ready!                        ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  AppImage: ${BLD}$APPIMAGE_OUT${NC}"
echo -e "  Size:     $(du -sh "$APPIMAGE_OUT" | awk '{print $1}')"
echo ""
echo -e "  Run it directly — no installation needed:"
echo -e "    ${BLD}chmod +x $APPIMAGE_OUT${NC}   ${DIM}# already done${NC}"
echo -e "    ${BLD}$APPIMAGE_OUT${NC}"
echo ""
echo -e "  To integrate with your desktop environment:"
echo -e "    ${BLD}$APPIMAGE_OUT --appimage-integrate${NC}"
echo ""

# Clean up staging tree (keep tools cache to avoid re-downloading appimagetool)
rm -rf "$ROOT/.appimage_stage/AppDir"
