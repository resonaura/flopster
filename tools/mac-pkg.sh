#!/usr/bin/env bash
# =============================================================================
#  Flopster — Native macOS .pkg Installer Builder
#
#  Produces a double-clickable  Flopster-<version>.pkg  using only Apple's
#  own command-line tools (pkgbuild + productbuild).  No third-party deps.
#
#  Usage:
#    ./tools/mac-pkg.sh                  # build everything then create .pkg
#    ./tools/mac-pkg.sh --rebuild        # force clean rebuild first
#    ./tools/mac-pkg.sh --no-build       # skip build, use existing artefacts
#    ./tools/mac-pkg.sh --out <dir>      # write .pkg to <dir> instead of dist/
#    ./tools/mac-pkg.sh --help
#
#  Requirements (all ship with Xcode CLT):
#    pkgbuild, productbuild, cmake, ninja
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
AU_SRC="$ARTEFACTS/AU/Flopster.component"
APP_SRC="$ARTEFACTS/Standalone/Flopster.app"

# Staging roots (temporary, wiped each run)
STAGE="$ROOT/.pkg_stage"
STAGE_VST3="$STAGE/vst3"
STAGE_AU="$STAGE/au"
STAGE_APP="$STAGE/standalone"
STAGE_TOOLS="$STAGE/tools"
STAGE_RESOURCES="$STAGE/resources"
STAGE_PKGS="$STAGE/pkgs"

VERSION="1.24"
PKG_ID_PREFIX="com.shiru.flopster"

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
    echo "    --rebuild       Force a clean CMake rebuild before packaging."
    echo "    --no-build      Skip the build step entirely (use existing artefacts)."
    echo "    --out <dir>     Write the final .pkg to <dir>  (default: dist/)"
    echo "    --help, -h      Show this help."
    echo ""
    echo "  Output:"
    echo "    dist/Flopster-<version>.pkg"
    echo ""
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rebuild)    REBUILD=1;      shift ;;
        --no-build)   NO_BUILD=1;     shift ;;
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
echo -e "${CYN}${BLD}║   📦  Flopster .pkg Installer Builder        ║${NC}"
echo -e "${CYN}${BLD}║       by Shiru & Resonaura                   ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""

# ── Prerequisites ─────────────────────────────────────────────────────────────
step "Checking prerequisites"
ruler

for tool in pkgbuild productbuild cmake ninja; do
    if ! command -v "$tool" &>/dev/null; then
        case "$tool" in
            pkgbuild|productbuild)
                die "$tool not found — install Xcode Command Line Tools:  xcode-select --install" ;;
            cmake)  die "cmake not found — install with:  brew install cmake" ;;
            ninja)  die "ninja not found — install with:  brew install ninja" ;;
        esac
    fi
    ok "$tool found"
done

# ── Build ─────────────────────────────────────────────────────────────────────
if [[ $NO_BUILD -eq 0 ]]; then
    step "Building Flopster"
    ruler

    ARTEFACT_BINARY="$VST3_SRC/Contents/MacOS/Flopster"

    if [[ $REBUILD -eq 1 ]] && [[ -d "$BUILD" ]]; then
        info "--rebuild: wiping old build directory..."
        rm -rf "$BUILD"
        ok "Old build removed."
    fi

    if [[ $REBUILD -eq 1 ]] || [[ ! -f "$ARTEFACT_BINARY" ]]; then
        CC="$(xcrun --find clang)"
        CXX="$(xcrun --find clang++)"
        JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

        info "Configuring…"
        mkdir -p "$BUILD"
        cmake -S "$ROOT" -B "$BUILD" \
            -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            "-DCMAKE_C_COMPILER=$CC" \
            "-DCMAKE_CXX_COMPILER=$CXX" \
            2>&1 | sed 's/^/    /' \
            || die "CMake configuration failed."
        ok "Configured."

        info "Building ($JOBS cores)…"
        ninja -C "$BUILD" -j"$JOBS" 2>&1 | sed 's/^/    /' \
            || die "Build failed. Run  ninja -C $BUILD  for details."
        ok "Build succeeded."

        if [[ -f "$BUILD/compile_commands.json" ]]; then
            cp "$BUILD/compile_commands.json" "$ROOT/compile_commands.json"
            ok "compile_commands.json synced."
        fi
    else
        ok "Artefacts already present — skipping build.  (Pass --rebuild to force.)"
    fi
else
    info "--no-build: using existing artefacts."
fi

# ── Verify artefacts ──────────────────────────────────────────────────────────
step "Verifying source artefacts"
ruler

[[ -d "$VST3_SRC"        ]] || die "VST3 artefact not found:       $VST3_SRC"
[[ -d "$AU_SRC"          ]] || die "AU artefact not found:         $AU_SRC"
[[ -d "$APP_SRC"         ]] || die "Standalone artefact not found: $APP_SRC"
[[ -d "$SAMPLES"         ]] || die "samples/ directory not found"

ok "Flopster.vst3"
ok "Flopster.component"
ok "Flopster.app"
ok "Assets & samples"

# ── Ad-hoc sign artefacts before staging ──────────────────────────────────────
step "Ad-hoc codesigning artefacts"
ruler

adhoc_sign() {
    local bundle="$1"
    xattr -rd com.apple.quarantine "$bundle" 2>/dev/null || true
    codesign --force --deep --sign - "$bundle" 2>/dev/null \
        || warn "codesign returned non-zero for $(basename "$bundle") — continuing anyway"
    ok "Signed $(basename "$bundle")"
}

adhoc_sign "$VST3_SRC"
adhoc_sign "$AU_SRC"
adhoc_sign "$APP_SRC"

# ── Stage component roots ──────────────────────────────────────────────────────
step "Staging component payloads"
ruler

rm -rf "$STAGE"
mkdir -p "$STAGE_PKGS" "$STAGE_RESOURCES"

copy_resources_into() {
    local bundle_res="$1"
    mkdir -p "$bundle_res"
    [[ -f "$ASSETS/scanlines.png" ]] && cp "$ASSETS/scanlines.png" "$bundle_res/"
    rm -rf "$bundle_res/samples"
    cp -R  "$SAMPLES"     "$bundle_res/samples"
}

# ─ VST3 ───────────────────────────────────────────────────────────────────────
VST3_PAYLOAD="$STAGE_VST3/Library/Audio/Plug-Ins/VST3/Flopster.vst3"
mkdir -p "$(dirname "$VST3_PAYLOAD")"
cp -R "$VST3_SRC" "$VST3_PAYLOAD"
copy_resources_into "$VST3_PAYLOAD/Contents/Resources"
ok "VST3 payload staged"

# ─ AU ─────────────────────────────────────────────────────────────────────────
AU_PAYLOAD="$STAGE_AU/Library/Audio/Plug-Ins/Components/Flopster.component"
mkdir -p "$(dirname "$AU_PAYLOAD")"
cp -R "$AU_SRC" "$AU_PAYLOAD"
copy_resources_into "$AU_PAYLOAD/Contents/Resources"
ok "AU payload staged"

# ─ Standalone ─────────────────────────────────────────────────────────────────
APP_PAYLOAD="$STAGE_APP/Applications/Flopster.app"
mkdir -p "$(dirname "$APP_PAYLOAD")"
cp -R "$APP_SRC" "$APP_PAYLOAD"
copy_resources_into "$APP_PAYLOAD/Contents/Resources"
mkdir -p "$APP_PAYLOAD/Contents/MacOS"
[[ -f "$ASSETS/scanlines.png" ]] && cp "$ASSETS/scanlines.png" "$APP_PAYLOAD/Contents/MacOS/"
rm -rf "$APP_PAYLOAD/Contents/MacOS/samples"
cp -R  "$SAMPLES"     "$APP_PAYLOAD/Contents/MacOS/samples"
ok "Standalone payload staged"

# ─ Tools (uninstaller) — empty payload, installed via postinstall script ───────
mkdir -p "$STAGE_TOOLS"
ok "Tools payload staged (empty — uninstaller written by postinstall)"

# ── Build scripts ──────────────────────────────────────────────────────────────
step "Building installer scripts"
ruler

# ─ VST3 scripts ───────────────────────────────────────────────────────────────
VST3_SCRIPTS="$STAGE/vst3_scripts"
mkdir -p "$VST3_SCRIPTS"

cat > "$VST3_SCRIPTS/preinstall" << 'PREINSTALL'
#!/usr/bin/env bash
for DIR in \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components" \
    "/Applications"; do
    mkdir -p "$DIR" 2>/dev/null || true
    chmod 755 "$DIR" 2>/dev/null || true
    chown "$USER" "$DIR" 2>/dev/null || true
done
chflags -R nouchg "$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true
TARGET="$HOME/Library/Audio/Plug-Ins/VST3/Flopster.vst3"
if [[ -e "$TARGET" ]]; then
    chflags -R nouchg "$TARGET" 2>/dev/null || true
    rm -rf "$TARGET"
fi
exit 0
PREINSTALL
chmod +x "$VST3_SCRIPTS/preinstall"

cat > "$VST3_SCRIPTS/postinstall" << 'POSTINSTALL'
#!/usr/bin/env bash
for DIR in \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components" \
    "/Applications"; do
    mkdir -p "$DIR" 2>/dev/null || true
    chmod 755 "$DIR" 2>/dev/null || true
    chown "$USER" "$DIR" 2>/dev/null || true
done
chflags -R nouchg "$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true
exit 0
POSTINSTALL
chmod +x "$VST3_SCRIPTS/postinstall"
ok "VST3 scripts"

# ─ AU scripts ─────────────────────────────────────────────────────────────────
AU_SCRIPTS="$STAGE/au_scripts"
mkdir -p "$AU_SCRIPTS"

cat > "$AU_SCRIPTS/preinstall" << 'PREINSTALL'
#!/usr/bin/env bash
for DIR in \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components" \
    "/Applications"; do
    mkdir -p "$DIR" 2>/dev/null || true
    chmod 755 "$DIR" 2>/dev/null || true
    chown "$USER" "$DIR" 2>/dev/null || true
done
chflags -R nouchg "$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true
TARGET="$HOME/Library/Audio/Plug-Ins/Components/Flopster.component"
if [[ -e "$TARGET" ]]; then
    chflags -R nouchg "$TARGET" 2>/dev/null || true
    rm -rf "$TARGET"
fi
exit 0
PREINSTALL
chmod +x "$AU_SCRIPTS/preinstall"

cat > "$AU_SCRIPTS/postinstall" << 'POSTINSTALL'
#!/usr/bin/env bash
for DIR in \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components" \
    "/Applications"; do
    mkdir -p "$DIR" 2>/dev/null || true
    chmod 755 "$DIR" 2>/dev/null || true
    chown "$USER" "$DIR" 2>/dev/null || true
done
chflags -R nouchg "$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true
# Refresh AU cache so the plugin appears immediately in DAWs
if command -v pluginkit &>/dev/null; then
    pluginkit -e use    -i com.shiru.flopster 2>/dev/null || true
    pluginkit -e ignore -i com.shiru.flopster 2>/dev/null || true
    pluginkit -e use    -i com.shiru.flopster 2>/dev/null || true
fi
killall -9 coreaudiod 2>/dev/null || true
exit 0
POSTINSTALL
chmod +x "$AU_SCRIPTS/postinstall"
ok "AU scripts"

# ─ Standalone scripts ─────────────────────────────────────────────────────────
APP_SCRIPTS="$STAGE/app_scripts"
mkdir -p "$APP_SCRIPTS"

cat > "$APP_SCRIPTS/preinstall" << 'PREINSTALL'
#!/usr/bin/env bash
for DIR in \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components" \
    "/Applications"; do
    mkdir -p "$DIR" 2>/dev/null || true
    chmod 755 "$DIR" 2>/dev/null || true
    chown "$USER" "$DIR" 2>/dev/null || true
done
chflags -R nouchg "$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true
TARGET="/Applications/Flopster.app"
if [[ -e "$TARGET" ]]; then
    chflags -R nouchg "$TARGET" 2>/dev/null || true
    rm -rf "$TARGET"
fi
exit 0
PREINSTALL
chmod +x "$APP_SCRIPTS/preinstall"

cat > "$APP_SCRIPTS/postinstall" << 'POSTINSTALL'
#!/usr/bin/env bash
for DIR in \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components" \
    "/Applications"; do
    mkdir -p "$DIR" 2>/dev/null || true
    chmod 755 "$DIR" 2>/dev/null || true
    chown "$USER" "$DIR" 2>/dev/null || true
done
chflags -R nouchg "$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true
exit 0
POSTINSTALL
chmod +x "$APP_SCRIPTS/postinstall"
ok "Standalone scripts"

# ─ Tools scripts — installs the uninstaller .command ──────────────────────────
TOOLS_SCRIPTS="$STAGE/tools_scripts"
mkdir -p "$TOOLS_SCRIPTS"

cat > "$TOOLS_SCRIPTS/preinstall" << 'PREINSTALL'
#!/usr/bin/env bash
for DIR in \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components" \
    "/Applications"; do
    mkdir -p "$DIR" 2>/dev/null || true
    chmod 755 "$DIR" 2>/dev/null || true
    chown "$USER" "$DIR" 2>/dev/null || true
done
chflags -R nouchg "$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true
TARGET="/Applications/Flopster Uninstaller.command"
if [[ -e "$TARGET" ]]; then
    chflags nouchg "$TARGET" 2>/dev/null || true
    rm -f "$TARGET"
fi
exit 0
PREINSTALL
chmod +x "$TOOLS_SCRIPTS/preinstall"

# NOTE: outer heredoc is UNQUOTED so $UNINSTALLER expands when the script runs
# on the user's machine.  Inner heredoc content is written with printf so there
# is no nesting ambiguity.
cat > "$TOOLS_SCRIPTS/postinstall" << POSTINSTALL
#!/usr/bin/env bash
for DIR in \\
    "\$HOME/Library/Audio/Plug-Ins/VST3" \\
    "\$HOME/Library/Audio/Plug-Ins/Components" \\
    "/Applications"; do
    mkdir -p "\$DIR" 2>/dev/null || true
    chmod 755 "\$DIR" 2>/dev/null || true
    chown "\$USER" "\$DIR" 2>/dev/null || true
done
chflags -R nouchg "\$HOME/Library/Audio/Plug-Ins" 2>/dev/null || true

UNINSTALLER="/Applications/Flopster Uninstaller.command"

printf '%s\n' \\
    '#!/usr/bin/env bash' \\
    '' \\
    'echo ""' \\
    'echo "Flopster Uninstaller"' \\
    'echo "--------------------"' \\
    'echo "Removing Flopster VST3..."' \\
    'rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Flopster.vst3"' \\
    'echo "Removing Flopster AU..."' \\
    'rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Flopster.component"' \\
    'echo "Removing Flopster Standalone..."' \\
    'rm -rf "/Applications/Flopster.app"' \\
    'echo "Removing Flopster Uninstaller..."' \\
    'rm -f "/Applications/Flopster Uninstaller.command"' \\
    'echo ""' \\
    'echo "Done. Flopster has been uninstalled."' \\
    'echo ""' \\
    > "\$UNINSTALLER"

chmod +x "\$UNINSTALLER"
echo "Flopster Uninstaller installed to /Applications/Flopster Uninstaller.command"
exit 0
POSTINSTALL
chmod +x "$TOOLS_SCRIPTS/postinstall"
ok "Tools (uninstaller) scripts"

# ── Build component .pkg files ────────────────────────────────────────────────
step "Building component packages"
ruler

pkgbuild \
    --root             "$STAGE_VST3" \
    --identifier       "$PKG_ID_PREFIX.vst3" \
    --version          "$VERSION" \
    --install-location / \
    --scripts          "$VST3_SCRIPTS" \
    "$STAGE_PKGS/Flopster-VST3.pkg" \
    || die "pkgbuild failed for VST3"
ok "Flopster-VST3.pkg"

pkgbuild \
    --root             "$STAGE_AU" \
    --identifier       "$PKG_ID_PREFIX.au" \
    --version          "$VERSION" \
    --install-location / \
    --scripts          "$AU_SCRIPTS" \
    "$STAGE_PKGS/Flopster-AU.pkg" \
    || die "pkgbuild failed for AU"
ok "Flopster-AU.pkg"

pkgbuild \
    --root             "$STAGE_APP" \
    --identifier       "$PKG_ID_PREFIX.standalone" \
    --version          "$VERSION" \
    --install-location / \
    --scripts          "$APP_SCRIPTS" \
    "$STAGE_PKGS/Flopster-Standalone.pkg" \
    || die "pkgbuild failed for Standalone"
ok "Flopster-Standalone.pkg"

pkgbuild \
    --root             "$STAGE_TOOLS" \
    --identifier       "$PKG_ID_PREFIX.tools" \
    --version          "$VERSION" \
    --install-location / \
    --scripts          "$TOOLS_SCRIPTS" \
    "$STAGE_PKGS/Flopster-Tools.pkg" \
    || die "pkgbuild failed for Tools"
ok "Flopster-Tools.pkg"

# ── Installer resources ───────────────────────────────────────────────────────
step "Generating installer resources"
ruler

# ── welcome.html ───────────────────────────────────────────────────────────────
cat > "$STAGE_RESOURCES/welcome.html" << HTML
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"/>
<style>
  body { font-family: "Helvetica Neue", Helvetica, Arial, sans-serif;
         font-size: 13px; color: #1d1d1f; margin: 20px; }
  h2   { font-size: 17px; margin-bottom: 8px; }
  p    { line-height: 1.6; margin: 6px 0; }
  b    { font-weight: 600; }
</style>
</head>
<body>
  <h2>Flopster ${VERSION}</h2>
  <p>Floppy drive instrument by Shiru &amp; Resonaura.</p>
  <p>Click <b>Continue</b> to choose what to install.</p>
</body>
</html>
HTML
ok "welcome.html"

# ── readme.html ────────────────────────────────────────────────────────────────
cat > "$STAGE_RESOURCES/readme.html" << HTML
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"/>
<style>
  body { font-family: "Helvetica Neue", Helvetica, Arial, sans-serif;
         font-size: 13px; color: #1d1d1f; margin: 20px; }
  h2   { font-size: 16px; margin-bottom: 6px; }
  ul   { padding-left: 18px; line-height: 1.8; margin: 6px 0; }
  code { font-family: "SF Mono", Menlo, Monaco, monospace;
         background: #1d1d1f; color: #f5f5f7;
         padding: 2px 6px; border-radius: 4px; font-size: 11px; }
  pre  { background: #1d1d1f; color: #f5f5f7; border-radius: 6px;
         padding: 8px 10px; font-size: 11px; line-height: 1.5; }
  .dim { color: #6e6e73; font-size: 11px; }
</style>
</head>
<body>
  <h2>Flopster ${VERSION}</h2>
  <ul>
    <li>After install, restart your DAW and trigger a plugin rescan.</li>
    <li>Standalone app: <b>/Applications/Flopster.app</b></li>
    <li>To uninstall, double-click <b>Flopster Uninstaller</b> in Applications.</li>
  </ul>
  <h2 style="margin-top:14px;">If the installer doesn't open</h2>
  <p class="dim">Run this in Terminal to remove the quarantine flag:</p>
  <pre>sudo xattr -rd com.apple.quarantine /path/to/Flopster.pkg</pre>
</body>
</html>
HTML
ok "readme.html"

# ── conclusion.html ────────────────────────────────────────────────────────────
cat > "$STAGE_RESOURCES/conclusion.html" << HTML
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"/>
<style>
  body { font-family: "Helvetica Neue", Helvetica, Arial, sans-serif;
         font-size: 13px; color: #1d1d1f; margin: 20px; }
  h2   { font-size: 17px; margin-bottom: 8px; }
  p    { line-height: 1.6; margin: 6px 0; }
  ul   { padding-left: 18px; line-height: 1.8; }
</style>
</head>
<body>
  <h2>Installation complete! 🎹</h2>
  <p>Flopster has been installed. Restart your DAW to pick up the plugin.</p>
  <ul>
    <li>Standalone: <b>/Applications/Flopster.app</b></li>
    <li>Uninstaller: <b>/Applications/Flopster Uninstaller.command</b></li>
  </ul>
</body>
</html>
HTML
ok "conclusion.html"

# ── Distribution XML ───────────────────────────────────────────────────────────
step "Generating distribution XML"
ruler

cat > "$STAGE_RESOURCES/distribution.xml" << XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">

    <!-- ── Metadata ──────────────────────────────────────────────────── -->
    <title>Flopster ${VERSION}</title>
    <organization>com.shiru</organization>

    <!-- ── OS requirement ────────────────────────────────────────────── -->
    <allowed-os-versions>
        <os-version min="12.0"/>
    </allowed-os-versions>

    <!-- ── Installer UI pages ────────────────────────────────────────── -->
    <welcome    file="welcome.html"    mime-type="text/html"/>
    <readme     file="readme.html"     mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>

    <!-- ── Options ───────────────────────────────────────────────────── -->
    <!--
        customize="always" opens the Customize pane by default instead of
        requiring the user to click the Customize button.
    -->
    <options customize="always" require-scripts="false"
             hostArchitectures="x86_64,arm64"/>

    <!-- ── Component choices ─────────────────────────────────────────── -->
    <choices-outline>
        <line choice="${PKG_ID_PREFIX}.choice.vst3"/>
        <line choice="${PKG_ID_PREFIX}.choice.au"/>
        <line choice="${PKG_ID_PREFIX}.choice.standalone"/>
        <line choice="${PKG_ID_PREFIX}.choice.tools"/>
    </choices-outline>

    <choice id="${PKG_ID_PREFIX}.choice.vst3"
            title="VST3 Plugin"
            description="Installs Flopster.vst3 to ~/Library/Audio/Plug-Ins/VST3/"
            selected="true" enabled="true" visible="true">
        <pkg-ref id="${PKG_ID_PREFIX}.vst3"/>
    </choice>

    <choice id="${PKG_ID_PREFIX}.choice.au"
            title="Audio Unit (AU) Plugin"
            description="Installs Flopster.component to ~/Library/Audio/Plug-Ins/Components/ and refreshes the AU cache."
            selected="true" enabled="true" visible="true">
        <pkg-ref id="${PKG_ID_PREFIX}.au"/>
    </choice>

    <choice id="${PKG_ID_PREFIX}.choice.standalone"
            title="Standalone Application"
            description="Installs Flopster.app to /Applications/"
            selected="true" enabled="true" visible="true">
        <pkg-ref id="${PKG_ID_PREFIX}.standalone"/>
    </choice>

    <choice id="${PKG_ID_PREFIX}.choice.tools"
            title="Flopster Tools (Uninstaller)"
            description="Installs a double-clickable uninstaller to /Applications/Flopster Uninstaller.command"
            selected="true" enabled="false" visible="true">
        <pkg-ref id="${PKG_ID_PREFIX}.tools"/>
    </choice>

    <!-- ── Package references ────────────────────────────────────────── -->
    <pkg-ref id="${PKG_ID_PREFIX}.vst3"
             version="${VERSION}"
             auth="Root">Flopster-VST3.pkg</pkg-ref>

    <pkg-ref id="${PKG_ID_PREFIX}.au"
             version="${VERSION}"
             auth="Root">Flopster-AU.pkg</pkg-ref>

    <pkg-ref id="${PKG_ID_PREFIX}.standalone"
             version="${VERSION}"
             auth="Root">Flopster-Standalone.pkg</pkg-ref>

    <pkg-ref id="${PKG_ID_PREFIX}.tools"
             version="${VERSION}"
             auth="Root">Flopster-Tools.pkg</pkg-ref>

</installer-gui-script>
XML
ok "distribution.xml"

# ── Assemble final .pkg ────────────────────────────────────────────────────────
step "Assembling final .pkg"
ruler

mkdir -p "$OUT_DIR"
FINAL_PKG="$OUT_DIR/Flopster-${VERSION}.pkg"

productbuild \
    --distribution  "$STAGE_RESOURCES/distribution.xml" \
    --resources     "$STAGE_RESOURCES" \
    --package-path  "$STAGE_PKGS" \
    "$FINAL_PKG" \
    || die "productbuild failed."

ok "Package built: $FINAL_PKG"

# ── Cleanup staging area ───────────────────────────────────────────────────────
rm -rf "$STAGE"
ok "Staging area cleaned up."

# ── Summary ────────────────────────────────────────────────────────────────────
PKG_SIZE="$(du -sh "$FINAL_PKG" 2>/dev/null | awk '{print $1}')"
XATTR_CMD="sudo xattr -rd com.apple.quarantine \"$FINAL_PKG\""

echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   ✅  Package ready!                         ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  📦  ${BLD}$FINAL_PKG${NC}  (${PKG_SIZE})"
echo ""
echo -e "  Components:"
echo -e "   • VST3 plugin       — ~/Library/Audio/Plug-Ins/VST3/"
echo -e "   • AU plugin         — ~/Library/Audio/Plug-Ins/Components/  (+cache refresh)"
echo -e "   • Standalone app    — /Applications/Flopster.app"
echo -e "   • Uninstaller       — /Applications/Flopster Uninstaller.command"
echo ""
echo -e "  ${YLW}${BLD}If Gatekeeper blocks the installer, run:${NC}"
echo -e "  ${DIM}$XATTR_CMD${NC}"
echo ""
echo -e "  ${DIM}(Optional) Sign:  productsign --sign 'Developer ID Installer: …' \\${NC}"
echo -e "  ${DIM}                  $FINAL_PKG Flopster-${VERSION}-signed.pkg${NC}"
echo ""
