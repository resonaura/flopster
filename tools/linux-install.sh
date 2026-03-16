#!/usr/bin/env bash
# =============================================================================
#  Flopster — Linux Installer
#  Checks deps, builds (if needed), installs VST3 and/or Standalone,
#  copies bundled resources, and registers a .desktop entry.
#  by Shiru & Resonaura
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD="$ROOT/build"
ASSETS="$ROOT/assets"
SAMPLES="$ROOT/samples"

ARTEFACTS="$BUILD/Flopster_artefacts/Release"
VST3_SRC="$ARTEFACTS/VST3/Flopster.vst3"
BIN_SRC="$ARTEFACTS/Standalone/flopster"

VST3_DST="$HOME/.vst3/Flopster.vst3"
BIN_DST="$HOME/.local/bin/flopster"
DESKTOP_DST="$HOME/.local/share/applications/flopster.desktop"
ICON_DST="$HOME/.local/share/flopster/app.png"

VERSION="1.24"

# ── Colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; DIM='\033[2m'; NC='\033[0m'

info()  { echo -e "${CYN}${BLD}  →  $*${NC}"; }
ok()    { echo -e "${GRN}  ✅  $*${NC}"; }
warn()  { echo -e "${YLW}  ⚠️   $*${NC}"; }
die()   { echo -e "${RED}  ❌  $*${NC}" >&2; exit 1; }
step()  { echo -e "\n${BLD}${YLW}── $* ──${NC}"; }
ruler() { echo -e "${DIM}  ────────────────────────────────────────────────${NC}"; }

# ── Banner ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   🎹  Flopster Plugin Installer  v${VERSION}       ║${NC}"
echo -e "${CYN}${BLD}║       Linux Edition                          ║${NC}"
echo -e "${CYN}${BLD}║       by Shiru & Resonaura                   ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""

# ── Parse flags ───────────────────────────────────────────────────────────────
REBUILD=0
INSTALL_VST3=1
INSTALL_STANDALONE=1

show_help() {
    echo "  Usage: $0 [--rebuild] [--only <format>[,<format>...]] [--help]"
    echo ""
    echo "  Options:"
    echo "    --rebuild, -r          Force a clean rebuild even if artefacts exist."
    echo "    --only <formats>       Install only the specified format(s)."
    echo "                           Comma-separated list of: vst3, standalone"
    echo "    --help, -h             Show this help message."
    echo ""
    echo "  Examples:"
    echo "    $0                         # install all (VST3 + Standalone)"
    echo "    $0 --only standalone       # reinstall Standalone only"
    echo "    $0 --only vst3,standalone  # reinstall VST3 and Standalone"
    echo "    $0 --rebuild --only vst3   # force rebuild, then install VST3 only"
    echo ""
    echo "  Install locations:"
    echo "    VST3        →  ~/.vst3/Flopster.vst3"
    echo "    Standalone  →  ~/.local/bin/flopster"
    echo "    Desktop     →  ~/.local/share/applications/flopster.desktop"
    echo ""
    exit 0
}

parse_only() {
    local raw="$1"
    INSTALL_VST3=0
    INSTALL_STANDALONE=0
    IFS=',' read -ra PARTS <<< "$raw"
    for part in "${PARTS[@]}"; do
        local token
        token="$(echo "$part" | tr '[:upper:]' '[:lower:]' | tr -d ' ')"
        case "$token" in
            vst3)       INSTALL_VST3=1 ;;
            standalone) INSTALL_STANDALONE=1 ;;
            au)         warn "AU is not supported on Linux — skipping." ;;
            *)          die "Unknown format '${part}'. Valid values: vst3, standalone" ;;
        esac
    done
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rebuild|-r)
            REBUILD=1; shift ;;
        --only)
            [[ -z "${2:-}" ]] && die "--only requires an argument (e.g. --only vst3)"
            parse_only "$2"; shift 2 ;;
        --only=*)
            parse_only "${1#--only=}"; shift ;;
        --help|-h)
            show_help ;;
        *)
            warn "Unknown argument: $1  (ignoring)"; shift ;;
    esac
done

TARGETS=()
[[ $INSTALL_VST3        -eq 1 ]] && TARGETS+=("VST3")
[[ $INSTALL_STANDALONE  -eq 1 ]] && TARGETS+=("Standalone")

if [[ ${#TARGETS[@]} -eq 0 ]]; then
    die "Nothing to install. Check your --only arguments."
fi

info "Targets: ${TARGETS[*]}"

# ── 1. Prerequisites ──────────────────────────────────────────────────────────
step "Checking prerequisites"
ruler

MISSING_APT=()
MISSING_DNF=()

# ── Helper: check a pkg-config library ───────────────────────────────────────
check_pkgconfig() {
    local name="$1" pc_name="$2" apt_pkg="$3" dnf_pkg="$4"
    if pkg-config --exists "$pc_name" 2>/dev/null; then
        ok "$name ($(pkg-config --modversion "$pc_name" 2>/dev/null || echo 'found'))"
    else
        warn "$name not found  [pkg: $pc_name]"
        MISSING_APT+=("$apt_pkg")
        MISSING_DNF+=("$dnf_pkg")
    fi
}

# cmake
if ! command -v cmake &>/dev/null; then
    die "cmake not found.\n  Ubuntu/Debian:  sudo apt-get install cmake\n  Fedora:         sudo dnf install cmake"
fi
ok "cmake $(cmake --version | head -1 | awk '{print $3}')"

# ninja
if ! command -v ninja &>/dev/null; then
    die "ninja not found.\n  Ubuntu/Debian:  sudo apt-get install ninja-build\n  Fedora:         sudo dnf install ninja-build"
fi
ok "ninja $(ninja --version)"

# pkg-config
if ! command -v pkg-config &>/dev/null; then
    die "pkg-config not found.\n  Ubuntu/Debian:  sudo apt-get install pkg-config\n  Fedora:         sudo dnf install pkgconf-pkg-config"
fi
ok "pkg-config $(pkg-config --version)"

# System libraries
check_pkgconfig "OpenGL (libGL)"   "gl"              "libgl1-mesa-dev"        "mesa-libGL-devel"
check_pkgconfig "ALSA"             "alsa"            "libasound2-dev"         "alsa-lib-devel"
check_pkgconfig "FreeType"         "freetype2"       "libfreetype6-dev"       "freetype-devel"
check_pkgconfig "Fontconfig"       "fontconfig"      "libfontconfig1-dev"     "fontconfig-devel"

# WebKit2GTK — try 4.1 first, fall back to 4.0
if pkg-config --exists "webkit2gtk-4.1" 2>/dev/null; then
    ok "WebKit2GTK 4.1 ($(pkg-config --modversion webkit2gtk-4.1))"
elif pkg-config --exists "webkit2gtk-4.0" 2>/dev/null; then
    ok "WebKit2GTK 4.0 ($(pkg-config --modversion webkit2gtk-4.0))"
else
    warn "WebKit2GTK not found  [pkg: webkit2gtk-4.1 or webkit2gtk-4.0]"
    MISSING_APT+=("libwebkit2gtk-4.1-dev")
    MISSING_DNF+=("webkit2gtk4.1-devel")
fi

if [[ ${#MISSING_APT[@]} -gt 0 ]]; then
    echo ""
    echo -e "${RED}${BLD}  Missing system libraries detected.${NC}"
    echo -e "  Install them with one of the following commands:\n"
    echo -e "  ${BLD}Ubuntu / Debian:${NC}"
    echo -e "    sudo apt-get install ${MISSING_APT[*]}"
    echo ""
    echo -e "  ${BLD}Fedora / RHEL:${NC}"
    echo -e "    sudo dnf install ${MISSING_DNF[*]}"
    echo ""
    die "Please install the missing libraries and re-run this script."
fi

# JUCE
if [ ! -d "$ROOT/JUCE/modules" ]; then
    warn "JUCE not found — cloning JUCE 8.0.7…"
    if ! command -v git &>/dev/null; then
        die "git not found. Install with:  sudo apt-get install git  or  sudo dnf install git"
    fi
    git clone --depth 1 --branch 8.0.7 \
        https://github.com/juce-framework/JUCE.git "$ROOT/JUCE" \
        || die "Failed to clone JUCE"
    ok "JUCE cloned"
else
    ok "JUCE found"
fi

# ── 2. Build if needed ────────────────────────────────────────────────────────
step "Build check"
ruler

ARTEFACT_BINARY="$VST3_SRC/Contents/x86_64-linux/Flopster.so"
# ARM64 fallback path
if [[ "$(uname -m)" == "aarch64" ]]; then
    ARTEFACT_BINARY="$VST3_SRC/Contents/aarch64-linux/Flopster.so"
fi

if [ "$REBUILD" -eq 1 ] || [ ! -f "$ARTEFACT_BINARY" ]; then
    if [ "$REBUILD" -eq 1 ] && [ -d "$BUILD" ]; then
        info "🗑️  --rebuild: wiping old build directory..."
        rm -rf "$BUILD"
        ok "Old build removed."
    fi

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
else
    ok "Release artefacts already present — skipping build.  (Pass --rebuild to force.)"
fi

# ── Sync compile_commands.json to repo root ───────────────────────────────────
if [ -f "$BUILD/compile_commands.json" ]; then
    cp "$BUILD/compile_commands.json" "$ROOT/compile_commands.json"
    ok "compile_commands.json synced to repo root."
fi

# ── 3. Verify source artefacts ────────────────────────────────────────────────
step "Verifying source artefacts"
ruler

if [[ $INSTALL_VST3 -eq 1 ]]; then
    [ -d "$VST3_SRC" ] || die "VST3 artefact not found: $VST3_SRC"
    ok "Found: Flopster.vst3"
fi

if [[ $INSTALL_STANDALONE -eq 1 ]]; then
    [ -f "$BIN_SRC" ] || die "Standalone binary not found: $BIN_SRC"
    ok "Found: flopster (standalone)"
fi

[ -f "$ASSETS/app.png" ]    || die "app.png not found in assets/"
[ -d "$SAMPLES" ]           || die "samples/ directory not found"
ok "Assets and samples directory verified."

# ── Helpers ───────────────────────────────────────────────────────────────────
copy_resources() {
    local dest="$1"
    mkdir -p "$dest"
    [[ -f "$ASSETS/scanlines.png" ]] && cp "$ASSETS/scanlines.png" "$dest/"
    rm -rf "$dest/samples"
    cp -R "$SAMPLES" "$dest/samples"
}

# ── 4. Install VST3 ───────────────────────────────────────────────────────────
if [[ $INSTALL_VST3 -eq 1 ]]; then
    step "Installing VST3 → $VST3_DST"
    ruler

    mkdir -p "$(dirname "$VST3_DST")"
    rm -rf "$VST3_DST"
    cp -R "$VST3_SRC" "$VST3_DST"
    ok "Bundle copied."

    copy_resources "$VST3_DST/Contents/Resources"
    ok "Resources copied."
else
    info "⏭️  Skipping VST3 (not in --only list)"
fi

# ── 5. Install Standalone ─────────────────────────────────────────────────────
if [[ $INSTALL_STANDALONE -eq 1 ]]; then
    step "Installing Standalone → $BIN_DST"
    ruler

    mkdir -p "$(dirname "$BIN_DST")"
    cp "$BIN_SRC" "$BIN_DST"
    chmod +x "$BIN_DST"
    ok "Binary installed."

    # Icon
    mkdir -p "$(dirname "$ICON_DST")"
    cp "$ASSETS/app.png" "$ICON_DST"
    ok "Icon copied → $ICON_DST"

    # .desktop file
    mkdir -p "$(dirname "$DESKTOP_DST")"
    cat > "$DESKTOP_DST" <<EOF
[Desktop Entry]
Name=Flopster
Comment=Floppy drive instrument
Exec=$BIN_DST
Icon=$ICON_DST
Type=Application
Categories=Audio;Music;
EOF
    ok ".desktop file written → $DESKTOP_DST"

    # Refresh application menu (best-effort; may not be present on all desktops)
    if command -v update-desktop-database &>/dev/null; then
        update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
        ok "Desktop database updated."
    fi
else
    info "⏭️  Skipping Standalone (not in --only list)"
fi

# ── 6. Summary ────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   ✅  Installation complete!                 ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
[[ $INSTALL_VST3       -eq 1 ]] && echo -e "  VST3        →  ${BLD}$VST3_DST${NC}"
[[ $INSTALL_STANDALONE -eq 1 ]] && echo -e "  Standalone  →  ${BLD}$BIN_DST${NC}"
[[ $INSTALL_STANDALONE -eq 1 ]] && echo -e "  Desktop     →  ${BLD}$DESKTOP_DST${NC}"
echo ""
echo -e "  Next steps:"
echo -e "  • Restart your DAW or trigger a plugin rescan."
echo -e "  • Most DAWs scan  ~/.vst3  automatically on startup."
if [[ $INSTALL_STANDALONE -eq 1 ]]; then
    echo -e "  • Make sure  ~/.local/bin  is in your PATH, then run:  flopster"
    echo -e "  • Or launch directly:  $BIN_DST"
fi
echo ""
