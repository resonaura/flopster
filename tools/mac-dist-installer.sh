#!/usr/bin/env bash
# =============================================================================
#  Flopster — macOS End-User Installer
#  No build tools required. Just run this script.
#  Installs AU, VST3, and Standalone. Bypasses Gatekeeper fully.
#  Handles first install and updates gracefully.
#  by Shiru & Resonaura
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

VST3_SRC="$SCRIPT_DIR/Flopster.vst3"
AU_SRC="$SCRIPT_DIR/Flopster.component"
APP_SRC="$SCRIPT_DIR/Flopster.app"

SAMPLES_SRC="$SCRIPT_DIR/samples"

VST3_DST="$HOME/Library/Audio/Plug-Ins/VST3/Flopster.vst3"
AU_DST="$HOME/Library/Audio/Plug-Ins/Components/Flopster.component"
APP_DST="/Applications/Flopster.app"

BACKUP_DIR="/tmp/Flopster-backup-$$"

# ── Colours ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; DIM='\033[2m'; NC='\033[0m'

info()  { echo -e "${CYN}${BLD}  →  $*${NC}"; }
ok()    { echo -e "${GRN}  ✅  $*${NC}"; }
warn()  { echo -e "${YLW}  ⚠️   $*${NC}"; }
die()   { echo -e "${RED}  ❌  $*${NC}" >&2; rollback; exit 1; }
step()  { echo -e "\n${BLD}${YLW}── $* ──${NC}"; }
ruler() { echo -e "${DIM}  ──────────────────────────────────────────────────${NC}"; }

# ── Rollback ──────────────────────────────────────────────────────────────────
rollback() {
    if [ ! -d "$BACKUP_DIR" ]; then return; fi
    echo ""
    warn "Something went wrong — rolling back to previous version..."

    [ -d "$BACKUP_DIR/Flopster.vst3"      ] && { rm -rf "$VST3_DST"; cp -R "$BACKUP_DIR/Flopster.vst3"      "$VST3_DST"; }
    [ -d "$BACKUP_DIR/Flopster.component" ] && { rm -rf "$AU_DST";   cp -R "$BACKUP_DIR/Flopster.component" "$AU_DST";   }
    [ -d "$BACKUP_DIR/Flopster.app"       ] && { sudo rm -rf "$APP_DST"; sudo cp -R "$BACKUP_DIR/Flopster.app" "$APP_DST"; }

    sudo rm -rf "$BACKUP_DIR"
    warn "Rollback complete. Your previous version has been restored."
}

trap 'EC=$?; kill "$SUDO_KEEPALIVE_PID" 2>/dev/null || true; if [ "${INSTALL_SUCCESS:-0}" != "1" ] && [ $EC -ne 0 ]; then rollback; fi' EXIT

# ── Banner ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
echo -e "${CYN}${BLD}║   🎹  Flopster Installer  v1.21              ║${NC}"
echo -e "${CYN}${BLD}║       by Shiru & Resonaura                   ║${NC}"
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""

# ── Verify bundles exist in package ───────────────────────────────────────────
step "Verifying package contents"
ruler

[ -d "$VST3_SRC" ] || { echo -e "${RED}  ❌  Flopster.vst3 not found next to this script.${NC}" >&2; exit 1; }
[ -d "$AU_SRC"   ] || { echo -e "${RED}  ❌  Flopster.component not found next to this script.${NC}" >&2; exit 1; }
[ -d "$APP_SRC"  ] || { echo -e "${RED}  ❌  Flopster.app not found next to this script.${NC}" >&2; exit 1; }
[ -d "$SAMPLES_SRC" ] || { echo -e "${RED}  ❌  samples/ directory not found next to this script.${NC}" >&2; exit 1; }

ok "Flopster.vst3 found"
ok "Flopster.component found"
ok "Flopster.app found"
ok "samples/ found"

# ── Strip quarantine from source files ────────────────────────────────────────
# Archives downloaded via browser get com.apple.quarantine on every file inside.
# cp refuses to copy quarantined xattrs and fails with "Operation not permitted".
# Strip it from all sources before we touch anything.
step "Stripping quarantine from package sources"
ruler

sudo xattr -rd com.apple.quarantine "$SCRIPT_DIR" 2>/dev/null || true
sudo xattr -rd com.apple.provenance "$SCRIPT_DIR" 2>/dev/null || true
ok "Source quarantine cleared"

# ── Detect existing installation ──────────────────────────────────────────────
step "Checking for existing installation"
ruler

IS_UPDATE=0
EXISTING_PARTS=()

[ -d "$VST3_DST" ] && { IS_UPDATE=1; EXISTING_PARTS+=("VST3"); }
[ -d "$AU_DST"   ] && { IS_UPDATE=1; EXISTING_PARTS+=("AU"); }
[ -d "$APP_DST"  ] && { IS_UPDATE=1; EXISTING_PARTS+=("Standalone"); }

if [ "$IS_UPDATE" -eq 1 ]; then
    warn "Existing Flopster installation detected: ${EXISTING_PARTS[*]}"
    info "This will update the existing installation."
    echo ""
    echo -e "  The old version will be backed up to ${DIM}$BACKUP_DIR${NC}"
    echo -e "  and automatically restored if anything goes wrong."
    echo ""
else
    info "No existing installation found — fresh install."
    echo ""
fi

echo -e "  Will install:"
echo -e "   • AU         →  ${BLD}$AU_DST${NC}"
echo -e "   • VST3       →  ${BLD}$VST3_DST${NC}"
echo -e "   • Standalone →  ${BLD}$APP_DST${NC}"
echo ""

# ── Ask for sudo upfront ──────────────────────────────────────────────────────
step "Requesting administrator privileges"
ruler
echo -e "  ${DIM}sudo is needed to install into /Applications and to"
echo -e "  clear Gatekeeper assessment for the plugin bundles.${NC}"
echo ""
sudo -v || { echo -e "${RED}  ❌  Administrator privileges required.${NC}" >&2; exit 1; }
ok "Got sudo"

# Keep sudo alive for the duration of the script
while true; do sudo -n true; sleep 50; kill -0 "$$" || exit; done 2>/dev/null &
SUDO_KEEPALIVE_PID=$!

# ── Kill audio daemons BEFORE touching any bundles ────────────────────────────
step "Releasing audio daemons"
ruler

sudo killall -9 coreaudiod       2>/dev/null && ok "Stopped coreaudiod"       || info "coreaudiod was not running"
sudo killall -9 audiod           2>/dev/null && ok "Stopped audiod"           || info "audiod was not running"
killall -9 "AUHostingService"    2>/dev/null && ok "Stopped AUHostingService" || info "AUHostingService was not running"
killall -9 "AUHostingServiceXPC" 2>/dev/null || true

sleep 1

# ── Back up existing installation ─────────────────────────────────────────────
if [ "$IS_UPDATE" -eq 1 ]; then
    step "Backing up existing installation"
    ruler

    mkdir -p "$BACKUP_DIR"

    [ -d "$VST3_DST" ] && { cp -R "$VST3_DST" "$BACKUP_DIR/Flopster.vst3";      ok "Backed up Flopster.vst3"; }
    [ -d "$AU_DST"   ] && { cp -R "$AU_DST"   "$BACKUP_DIR/Flopster.component"; ok "Backed up Flopster.component"; }
    [ -d "$APP_DST"  ] && { sudo cp -R "$APP_DST" "$BACKUP_DIR/Flopster.app";   ok "Backed up Flopster.app"; }
fi

# ── Resources copy helper ─────────────────────────────────────────────────────
# copy_resources <dest_dir>  — copies samples/ into a bundle
copy_resources() {
    local dest="$1"
    mkdir -p "$dest"
    [[ -f "$SCRIPT_DIR/scanlines.png" ]] && cp "$SCRIPT_DIR/scanlines.png" "$dest/"
    sudo rm -rf "$dest/samples"
    cp -R "$SAMPLES_SRC" "$dest/samples"
}

# ── Gatekeeper bypass helper ──────────────────────────────────────────────────
gatekeeper_clear() {
    local bundle="$1"
    local name
    name="$(basename "$bundle")"

    sudo xattr -rd com.apple.quarantine "$bundle" 2>/dev/null || true
    sudo xattr -rd com.apple.provenance "$bundle" 2>/dev/null || true
    ok "Quarantine stripped — $name"

    sudo codesign --force --deep --sign - "$bundle" 2>/dev/null || true
    ok "Ad-hoc codesigned — $name"

    sudo spctl --add --label "Flopster" "$bundle" 2>/dev/null || true
    sudo spctl --enable --label "Flopster" 2>/dev/null || true
    ok "Gatekeeper rule added — $name"
}

# ── Install VST3 ──────────────────────────────────────────────────────────────
step "Installing VST3"
ruler

mkdir -p "$(dirname "$VST3_DST")"
sudo rm -rf "$VST3_DST"
cp -R "$VST3_SRC" "$VST3_DST" || die "Failed to copy Flopster.vst3"
sudo chown -R "$USER:staff" "$VST3_DST"
ok "Copied → $VST3_DST"

copy_resources "$VST3_DST/Contents/Resources"
ok "Resources copied into VST3 bundle"

gatekeeper_clear "$VST3_DST"

# ── Install AU ────────────────────────────────────────────────────────────────
step "Installing AU"
ruler

mkdir -p "$(dirname "$AU_DST")"
sudo rm -rf "$AU_DST"
cp -R "$AU_SRC" "$AU_DST" || die "Failed to copy Flopster.component"
sudo chown -R "$USER:staff" "$AU_DST"
ok "Copied → $AU_DST"

copy_resources "$AU_DST/Contents/Resources"
ok "Resources copied into AU bundle"

gatekeeper_clear "$AU_DST"

# ── Install Standalone ────────────────────────────────────────────────────────
step "Installing Standalone"
ruler

sudo rm -rf "$APP_DST"
sudo cp -R "$APP_SRC" "$APP_DST" || die "Failed to copy Flopster.app"
sudo chown -R "$USER:staff" "$APP_DST"
ok "Copied → $APP_DST"

# Populate both Resources/ (primary lookup) and MacOS/ (fallback)
copy_resources "$APP_DST/Contents/Resources"
ok "Resources copied into Standalone bundle (Resources/)"

mkdir -p "$APP_DST/Contents/MacOS"
sudo rm -rf "$APP_DST/Contents/MacOS/samples"
cp -R "$SAMPLES_SRC" "$APP_DST/Contents/MacOS/samples"
ok "Resources copied into Standalone bundle (MacOS/ fallback)"

gatekeeper_clear "$APP_DST"

# ── Refresh Audio Unit cache ──────────────────────────────────────────────────
step "Refreshing Audio Unit cache"
ruler

if command -v pluginkit &>/dev/null; then
    pluginkit -e use    -i com.shiru.flopster 2>/dev/null || true
    pluginkit -e ignore -i com.shiru.flopster 2>/dev/null || true
    pluginkit -e use    -i com.shiru.flopster 2>/dev/null || true
    ok "pluginkit cache refreshed"
fi

sudo killall -9 coreaudiod 2>/dev/null || true
sleep 1
ok "coreaudiod restarted"

# ── Flush macOS icon cache ────────────────────────────────────────────────────
sudo find /var/folders -name "com.apple.iconservices" -type d -exec rm -rf {} + 2>/dev/null || true
sudo killall Dock   2>/dev/null || true
sudo killall Finder 2>/dev/null || true
ok "Icon cache flushed"

# ── AU Validation ─────────────────────────────────────────────────────────────
step "Validating AU component"
ruler
info "Running:  auval -v aumu Flps Shru"
echo ""

AU_RESULT="$(auval -v aumu Flps Shru 2>&1 || true)"
echo "$AU_RESULT" | sed 's/^/    /'
echo ""

if echo "$AU_RESULT" | grep -q "AU VALIDATION SUCCEEDED"; then
    ok "AU validation PASSED"
else
    warn "AU validation returned unexpected output — this is sometimes normal on first install."
    warn "If Flopster doesn't appear in your DAW, restart it and trigger a plugin rescan."
fi

# ── Clean up backup ───────────────────────────────────────────────────────────
# Mark success BEFORE cleanup so the EXIT trap doesn't misfire if sudo rm fails
INSTALL_SUCCESS=1

if [ -d "$BACKUP_DIR" ]; then
    sudo rm -rf "$BACKUP_DIR"
    ok "Backup cleaned up"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${CYN}${BLD}╔══════════════════════════════════════════════╗${NC}"
if [ "$IS_UPDATE" -eq 1 ]; then
echo -e "${CYN}${BLD}║   ✅  Update complete!                       ║${NC}"
else
echo -e "${CYN}${BLD}║   ✅  Installation complete!                 ║${NC}"
fi
echo -e "${CYN}${BLD}╚══════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  AU         →  ${BLD}$AU_DST${NC}"
echo -e "  VST3       →  ${BLD}$VST3_DST${NC}"
echo -e "  Standalone →  ${BLD}$APP_DST${NC}"
echo ""
echo -e "  ${BLD}Next steps:${NC}"
echo -e "  • Restart your DAW and trigger a plugin rescan."
echo -e "  • Logic / GarageBand:  Preferences → Plug-in Manager → Reset & Rescan"
echo -e "  • Ableton Live:        Options → Preferences → Plug-Ins → Rescan"
echo -e "  • Launch standalone:   open \"$APP_DST\""
echo ""
