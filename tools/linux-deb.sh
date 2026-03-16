#!/usr/bin/env bash
# =============================================================================
#  Flopster — Debian/Ubuntu .deb Package Builder
#
#  Produces a distributable  flopster_<version>_<arch>.deb  using only
#  dpkg-deb, which ships on every Debian/Ubuntu system.  No third-party
#  packaging tools required.
#
#  Usage:
#    ./tools/linux-deb.sh                  # build then create .deb
#    ./tools/linux-deb.sh --rebuild        # force clean rebuild first
#    ./tools/linux-deb.sh --no-build       # skip build, use existing artefacts
#    ./tools/linux-deb.sh --out <dir>      # write .deb to <dir>  (default: dist/)
#    ./tools/linux-deb.sh --help
#
#  Requirements:
#    cmake, ninja, dpkg-deb  (dpkg-deb ships on all Debian/Ubuntu systems)
#
#  Installs to:
#    /usr/lib/vst3/Flopster.vst3       (VST3 bundle + resources)
#    /usr/bin/flopster                  (standalone binary)
#    /usr/share/applications/flopster.desktop
#    /usr/share/flopster/               (scanlines.png, samples/)
#    /usr/share/icons/hicolor/256x256/apps/flopster.png
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
VST3_SRC="$ARTEFACTS/VST3/Flopster.vst3"
BIN_SRC="$ARTEFACTS/Standalone/flopster"

STAGE="$ROOT/.deb_stage"

VERSION="$(node -e "process.stdout.write(require('$ROOT/package.json').version)")"

# ── Architecture detection ────────────────────────────────────────────────────
case "$(uname -m)" in
    x86_64)  DEB_ARCH="amd64" ;;
    aarch64) DEB_ARCH="arm64" ;;
    armv7l)  DEB_ARCH="armhf" ;;
    *)       DEB_ARCH="$(uname -m)" ;;
esac

DEB_NAME="flopster_${VERSION}_${DEB_ARCH}.deb"

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
    echo "    --out <dir>      Write the final .deb to <dir>  (default: dist/)"
    echo "    --help, -h       Show this help."
    echo ""
    echo "  Output:"
    echo "    dist/flopster_${VERSION}_${DEB_ARCH}.deb"
    echo ""
    echo "  Install the resulting package with:"
    echo "    sudo dpkg -i dist/flopster_${VERSION}_${DEB_ARCH}.deb"
    echo ""
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rebuild)    REBUILD=1;     shift ;;
        --no-build)   NO_BUILD=1;    shift ;;
        --out)
            [[ -z "${2:-}" ]] && die "--out requires a directory argument"
            OUT_DIR="$2"; shift 2 ;;
        --out=*)      OUT_DIR="${1#--out=}"; shift ;;
        --help|-h)    show_help ;;
        *) warn "Unknown argument: $1  (ignoring)"; shift ;;
    esac
done

[[ $REBUILD -eq 1 && $NO_BUILD -eq 1 ]] && die "--rebuild and --no-build are mutually exclusive."

# ── Banner ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   📦  Flopster .deb Package Builder          ║${NC}"
echo -e "${CYN}${BLD}║       by Shiru & Resonaura                   ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
info "Version:      $VERSION"
info "Architecture: $DEB_ARCH"
info "Output:       $OUT_DIR/$DEB_NAME"

# ── 1. Prerequisites ──────────────────────────────────────────────────────────
step "Checking prerequisites"
ruler

for tool in cmake ninja dpkg-deb; do
    if ! command -v "$tool" &>/dev/null; then
        case "$tool" in
            cmake)    die "cmake not found.\n  Ubuntu/Debian:  sudo apt-get install cmake\n  Fedora:         sudo dnf install cmake" ;;
            ninja)    die "ninja not found.\n  Ubuntu/Debian:  sudo apt-get install ninja-build\n  Fedora:         sudo dnf install ninja-build" ;;
            dpkg-deb) die "dpkg-deb not found.\n  This tool ships with all Debian/Ubuntu systems (package: dpkg).\n  On Fedora you can create RPMs instead, or install dpkg:\n  sudo dnf install dpkg" ;;
        esac
    fi
    ok "$tool found"
done

if [ ! -d "$ROOT/JUCE/modules" ]; then
    if [[ $NO_BUILD -eq 1 ]]; then
        warn "JUCE directory not found, but --no-build was passed — skipping clone."
    else
        warn "JUCE not found — cloning JUCE 8.0.7…"
        if ! command -v git &>/dev/null; then
            die "git not found. Install with:  sudo apt-get install git"
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
    case "$(uname -m)" in
        x86_64)  ARTEFACT_BINARY="$VST3_SRC/Contents/x86_64-linux/Flopster.so" ;;
        aarch64) ARTEFACT_BINARY="$VST3_SRC/Contents/aarch64-linux/Flopster.so" ;;
        *)       ARTEFACT_BINARY="$VST3_SRC/Contents/$(uname -m)-linux/Flopster.so" ;;
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

[ -d "$VST3_SRC" ]        || die "VST3 bundle not found: $VST3_SRC"
[ -f "$BIN_SRC" ]         || die "Standalone binary not found: $BIN_SRC"
[ -f "$ASSETS/app.png" ]  || die "app.png not found in assets/"
[ -d "$SAMPLES" ]         || die "samples/ directory not found"

ok "VST3 bundle:       $VST3_SRC"
ok "Standalone binary: $BIN_SRC"
ok "Assets and samples verified."

# ── 4. Build staging tree ─────────────────────────────────────────────────────
step "Building .deb staging tree"
ruler

info "Wiping old staging directory…"
rm -rf "$STAGE"

# Directory layout
mkdir -p "$STAGE/DEBIAN"
mkdir -p "$STAGE/usr/lib/vst3"
mkdir -p "$STAGE/usr/bin"
mkdir -p "$STAGE/usr/share/applications"
mkdir -p "$STAGE/usr/share/flopster"
mkdir -p "$STAGE/usr/share/icons/hicolor/256x256/apps"

# VST3 bundle
info "Copying VST3 bundle…"
cp -R "$VST3_SRC" "$STAGE/usr/lib/vst3/Flopster.vst3"

# Resources into VST3 bundle
mkdir -p "$STAGE/usr/lib/vst3/Flopster.vst3/Contents/Resources"
[[ -f "$ASSETS/scanlines.png" ]] && cp "$ASSETS/scanlines.png" "$STAGE/usr/lib/vst3/Flopster.vst3/Contents/Resources/"
cp -R "$SAMPLES"      "$STAGE/usr/lib/vst3/Flopster.vst3/Contents/Resources/samples"
ok "VST3 bundle staged."

# Standalone binary
info "Copying standalone binary…"
cp "$BIN_SRC" "$STAGE/usr/bin/flopster"
chmod 755     "$STAGE/usr/bin/flopster"
ok "Standalone binary staged."

# Shared data (scanlines.png, samples/)
info "Copying shared resources…"
[[ -f "$ASSETS/scanlines.png" ]] && cp "$ASSETS/scanlines.png" "$STAGE/usr/share/flopster/"
cp -R "$SAMPLES"       "$STAGE/usr/share/flopster/samples"
ok "Shared resources staged."

# Icon
info "Copying icon…"
cp "$ASSETS/app.png" "$STAGE/usr/share/icons/hicolor/256x256/apps/flopster.png"
ok "Icon staged."

# .desktop file
info "Writing .desktop file…"
cat > "$STAGE/usr/share/applications/flopster.desktop" <<'DESKTOP'
[Desktop Entry]
Name=Flopster
Comment=Floppy drive instrument
Exec=/usr/bin/flopster
Icon=flopster
Type=Application
Categories=Audio;Music;
DESKTOP
ok ".desktop file staged."

# ── 5. DEBIAN/control ─────────────────────────────────────────────────────────
info "Writing DEBIAN/control…"

# Installed-Size: in kibibytes (approximate)
INSTALLED_KB="$(du -sk "$STAGE/usr" | awk '{print $1}')"

cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: flopster
Version: ${VERSION}
Architecture: ${DEB_ARCH}
Maintainer: Shiru & Resonaura <noreply@resonaura.com>
Installed-Size: ${INSTALLED_KB}
Depends: libasound2 | libasound2t64, libfreetype6 | libfreetype6t64, libfontconfig1, libgl1
Section: sound
Priority: optional
Description: Floppy drive instrument plugin (VST3 + Standalone)
 Flopster is a sampler-based virtual instrument that faithfully reproduces
 the sounds of vintage floppy disk drives. It ships as a VST3 plugin for
 use in any compatible DAW, and as a standalone application.
 .
 Original instrument design by Shiru. Linux port by Resonaura.
CONTROL
ok "control file written."

# ── 6. DEBIAN/postinst ────────────────────────────────────────────────────────
info "Writing DEBIAN/postinst…"
cat > "$STAGE/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e

chmod 755 /usr/bin/flopster

# Refresh icon cache (best-effort)
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
fi

# Refresh application menu database (best-effort)
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi

# Refresh shared library cache (best-effort)
if command -v ldconfig >/dev/null 2>&1; then
    ldconfig 2>/dev/null || true
fi

echo "Flopster installed successfully."
echo "  VST3 plugin:  /usr/lib/vst3/Flopster.vst3"
echo "  Standalone:   /usr/bin/flopster"
echo ""
echo "Restart your DAW or trigger a plugin rescan to discover the VST3."
POSTINST
chmod 755 "$STAGE/DEBIAN/postinst"
ok "postinst written."

# ── 7. DEBIAN/prerm ───────────────────────────────────────────────────────────
info "Writing DEBIAN/prerm…"
cat > "$STAGE/DEBIAN/prerm" <<'PRERM'
#!/bin/sh
set -e
echo "Removing Flopster…"
# Nothing to stop — Flopster has no background service or daemon.
# dpkg will remove all installed files automatically after this script.
PRERM
chmod 755 "$STAGE/DEBIAN/prerm"
ok "prerm written."

# ── 8. Fix permissions throughout ────────────────────────────────────────────
info "Fixing permissions…"
find "$STAGE/usr"    -type d -exec chmod 755 {} \;
find "$STAGE/usr"    -type f -exec chmod 644 {} \;
chmod 755 "$STAGE/usr/bin/flopster"
# VST3 shared objects need execute bits so the loader can map them
find "$STAGE/usr/lib/vst3" -name "*.so" -exec chmod 755 {} \;
ok "Permissions set."

# ── 9. Build the .deb ────────────────────────────────────────────────────────
step "Running dpkg-deb"
ruler

mkdir -p "$OUT_DIR"
DEB_OUT="$OUT_DIR/$DEB_NAME"

info "Building $DEB_OUT …"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB_OUT" \
    2>&1 | sed 's/^/    /' \
    || die "dpkg-deb failed."

ok "Package built: $DEB_OUT"

# ── 10. Summary ───────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   ✅  .deb package ready!                    ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  Package:  ${BLD}$DEB_OUT${NC}"
echo -e "  Size:     $(du -sh "$DEB_OUT" | awk '{print $1}')"
echo ""
echo -e "  Install with:"
echo -e "    ${BLD}sudo dpkg -i $DEB_OUT${NC}"
echo ""
echo -e "  Or install and automatically resolve dependencies:"
echo -e "    ${BLD}sudo apt-get install $DEB_OUT${NC}"
echo ""
echo -e "  Uninstall with:"
echo -e "    ${BLD}sudo apt-get remove flopster${NC}"
echo ""

# Clean up staging tree
rm -rf "$STAGE"
